#!/usr/bin/env bash
# Backlog point 6 (spec/23): `io.read` reads a block and does NOT stop at the end of the line
# — the spec had this wrong until 01.11.2026. Reading line by line did not exist; for that
# `io.read_line` is new. As a driver, because both need input on stdin.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML=./out/aiml/aiml
DIR=tests/io_read_line
fails=0

check() { if [ "$2" = "1" ]; then echo "  ok   $1"; else echo "  FAIL $1 (sah: $3)"; fails=$((fails+1)); fi; }

# 1) io.read_line returns ONE line, without the line break.
out=$(printf 'ab\ncd\n' | $AIML "$DIR/probe.aiml" 2>&1)
l1=$(printf '%s\n' "$out" | grep -oE 'L1=\[[^]]*\] len=[0-9]+' | head -1)
l2=$(printf '%s\n' "$out" | grep -oE 'L2=\[[^]]*\] len=[0-9]+' | head -1)
check "first line is 'ab' without a break" "$([ "$l1" = "L1=[ab] len=2" ] && echo 1)" "$l1"
check "second line is 'cd'"             "$([ "$l2" = "L2=[cd] len=2" ] && echo 1)" "$l2"

# 2) io.read stays a block read (6 bytes for "ab\ncd\n", not 3).
out=$(printf 'ab\ncd\n' | $AIML "$DIR/block.aiml" 2>&1)
blen=$(printf '%s\n' "$out" | grep -oE 'block_len=[0-9]+' | head -1)
check "io.read reads the whole block" "$([ "$blen" = "block_len=6" ] && echo 1)" "$blen"

# 3) At the end of input read_line returns an empty string, not a crash.
out=$(printf 'nur\n' | $AIML "$DIR/probe.aiml" 2>&1); rc=$?
check "read_line after EOF does not crash" "$([ $rc -eq 0 ] && echo 1)" "rc=$rc"

echo "io_read_line: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
[ $fails -eq 0 ]
