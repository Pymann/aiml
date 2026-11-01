#!/usr/bin/env bash
# Backlog-Punkt 6 (spec/23): `io.read` liest einen Block und bricht NICHT am Zeilenende ab
# — das stand bis 01.11.2026 falsch in der Spec. Zeilenweise lesen gab es nicht; dafuer
# ist `io.read_line` neu. Als Treiber, weil beides Eingaben auf stdin braucht.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML=./out/aiml/aiml
DIR=tests/io_read_line
fails=0

check() { if [ "$2" = "1" ]; then echo "  ok   $1"; else echo "  FAIL $1 (sah: $3)"; fails=$((fails+1)); fi; }

# 1) io.read_line liefert EINE Zeile, ohne den Zeilenumbruch.
out=$(printf 'ab\ncd\n' | $AIML "$DIR/probe.aiml" 2>&1)
l1=$(printf '%s\n' "$out" | grep -oE 'L1=\[[^]]*\] len=[0-9]+' | head -1)
l2=$(printf '%s\n' "$out" | grep -oE 'L2=\[[^]]*\] len=[0-9]+' | head -1)
check "erste Zeile ist 'ab' ohne Umbruch" "$([ "$l1" = "L1=[ab] len=2" ] && echo 1)" "$l1"
check "zweite Zeile ist 'cd'"             "$([ "$l2" = "L2=[cd] len=2" ] && echo 1)" "$l2"

# 2) io.read bleibt ein Blocklesen (6 Bytes fuer "ab\ncd\n", nicht 3).
out=$(printf 'ab\ncd\n' | $AIML "$DIR/block.aiml" 2>&1)
blen=$(printf '%s\n' "$out" | grep -oE 'block_len=[0-9]+' | head -1)
check "io.read liest den ganzen Block" "$([ "$blen" = "block_len=6" ] && echo 1)" "$blen"

# 3) Am Ende der Eingabe liefert read_line eine leere Zeichenkette, keinen Absturz.
out=$(printf 'nur\n' | $AIML "$DIR/probe.aiml" 2>&1); rc=$?
check "read_line nach EOF stuerzt nicht ab" "$([ $rc -eq 0 ] && echo 1)" "rc=$rc"

echo "io_read_line: $([ $fails -eq 0 ] && echo PASS || echo FAIL)"
[ $fails -eq 0 ]
