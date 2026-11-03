#!/usr/bin/env bash
# run.sh — storing a void call's result in a typed location must be REPORTED, not fatal.
#
# The whole point of this directory is the diagnosis.  A method that lost its `return:` used to
# take the compiler down with a bare SIGSEGV and no output at all (LLVM recursing in
# getPrefTypeAlignment until the stack was gone), so the author had nothing to go on but
# "Build failed".  Each neg/ case here must be rejected with its `# EXPECT-ERROR:` text AND a
# source location; the ok case must still compile and run.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/void_init"

echo "── void call into a typed location (diagnosis, not a crash) ──"
fail=0

for f in tests/void_init/neg_*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$f" -o /dev/null 2>&1); rc=$?
    if [ $rc -eq 139 ] || [ $rc -ge 128 ]; then
        printf "  FAIL [%s] crashed with rc=%d instead of reporting\n" "$base" "$rc"; fail=1; continue
    fi
    if [ $rc -eq 0 ]; then
        printf "  FAIL [%s] compiled, expected a rejection\n" "$base"; fail=1; continue
    fi
    if ! echo "$out" | grep -qF "$want"; then
        printf "  FAIL [%s] message lacks '%s'\n" "$base" "$want"
        echo "$out" | head -4 | sed 's/^/        /'
        fail=1; continue
    fi
    # the location is the point of the exercise: file:line must be named
    if ! echo "$out" | grep -qE "$base:[0-9]+"; then
        printf "  FAIL [%s] message has no source location\n" "$base"; fail=1; continue
    fi
    printf "  ok   [%s] rejected with a location\n" "$base"
done

for f in tests/void_init/ok_*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$f" -r 2>&1); rc=$?
    if [ $rc -ne 0 ]; then
        printf "  FAIL [%s] rc=%d\n" "$base" "$rc"; echo "$out" | tail -5 | sed 's/^/        /'; fail=1; continue
    fi
    printf "  ok   [%s] %s\n" "$base" "$(echo "$out" | grep -E 'PASS' | head -1)"
done

rm -rf "$CACHE"
if [ $fail -ne 0 ]; then echo "── void_init FAILED ──"; exit 1; fi
echo "── void_init PASS ──"
exit 0
