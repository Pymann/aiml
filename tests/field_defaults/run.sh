#!/usr/bin/env bash
# run.sh — spec/15 F15-40: a `list<T>` field only takes `[]` as default; a
# FILLED list default is a compile error.
#
# The point of this directory is the MESSAGE.  The check only existed for
# `- model:`/`fields:`; with `- class:`/`members:` the default was silently
# dropped, the field existed and was empty at runtime.  An empty list
# looks like a data problem, not like a compile error — in cai
# that cost three red tests and a probe that prints `.len`.
#
# Every neg/ case must be rejected with its `# EXPECT-ERROR:` text AND a location;
# the ok cases must keep compiling and running.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/field_defaults"

echo "── field defaults: a filled list default is an error (spec/15 F15-40) ──"
fail=0

for f in tests/field_defaults/neg_*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$f" -o /dev/null 2>&1); rc=$?
    if [ $rc -ge 128 ]; then
        printf "  FAIL [%s] crashed with rc=%d instead of reporting\n" "$base" "$rc"; fail=1; continue
    fi
    if [ $rc -eq 0 ]; then
        printf "  FAIL [%s] compiled, a rejection was expected\n" "$base"; fail=1; continue
    fi
    if ! echo "$out" | grep -qF "$want"; then
        printf "  FAIL [%s] message does not contain '%s'\n" "$base" "$want"
        echo "$out" | head -4 | sed 's/^/        /'
        fail=1; continue
    fi
    # The location is half the value of the message: file:line must be there.
    if ! echo "$out" | grep -qE "$base:[0-9]+"; then
        printf "  FAIL [%s] message names no location\n" "$base"
        echo "$out" | grep -iE 'error|list default' | head -2 | sed 's/^/        /'
        fail=1; continue
    fi
    printf "  ok   [%s] rejected, with location\n" "$base"
done

for f in tests/field_defaults/ok_*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$f" -r 2>&1); rc=$?
    if [ $rc -ne 0 ]; then
        printf "  FAIL [%s] rc=%d\n" "$base" "$rc"; echo "$out" | tail -6 | sed 's/^/        /'; fail=1; continue
    fi
    printf "  ok   [%s] %s\n" "$base" "$(echo "$out" | grep -E 'PASS' | head -1)"
done

rm -rf "$CACHE"
if [ $fail -ne 0 ]; then echo "── field_defaults FAILED ──"; exit 1; fi
echo "── field_defaults PASS ──"
exit 0
