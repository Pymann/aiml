#!/usr/bin/env bash
#
# Judge a run_all_tests.sh result file against the expected-failure register.
#
#   tools/check_expected_failures.sh <test_results.txt> [TEST_SUITES.md]
#
# The register is the table between the `expected-failures` markers in
# TEST_SUITES.md -- one source, read by people as a table and here as a list, so
# the two cannot drift apart.
#
# Exit 0 only when the run holds no surprise.  Two things are a surprise:
#
#   * a failure that is not registered -- a regression;
#   * a registered failure that PASSED -- the chapter landed, so the row has to
#     leave the register.  Without this half, a register rots into a list of
#     tests nobody looks at, and that is worse than no register at all.
#
# A registered entry the run never reached (skipped for want of a display, a
# peer, a toolchain) is reported but does not fail: a run that cannot execute a
# test has not learned anything about it.
set -u

RESULTS="${1:-}"
REGISTER="${2:-TEST_SUITES.md}"
if [ -z "$RESULTS" ]; then
    echo "usage: $0 <test_results.txt> [TEST_SUITES.md]" >&2
    exit 2
fi
for f in "$RESULTS" "$REGISTER"; do
    [ -r "$f" ] || { echo "cannot read $f" >&2; exit 2; }
done

expected=$(awk '
    /expected-failures:begin/ { inblock = 1; next }
    /expected-failures:end/   { inblock = 0 }
    inblock && /^\| *`/ {
        line = $0
        sub(/^\| *`/, "", line)
        sub(/`.*$/, "", line)
        if (line != "") print line
    }
' "$REGISTER" | sort -u)

if [ -z "$expected" ]; then
    echo "no expected-failure register found in $REGISTER" >&2
    echo "(are the expected-failures:begin/end markers still there?)" >&2
    exit 2
fi

failed=$(awk '$1 == "[" && $2 == "FAIL" { print $4 }' "$RESULTS" | sort -u)
passed=$(awk '$1 == "[" && $2 == "OK"   { print $4 }' "$RESULTS" | sort -u)

unexpected_fail=$(comm -23 <(echo "$failed")   <(echo "$expected"))
unexpected_pass=$(comm -12 <(echo "$expected") <(echo "$passed"))
not_run=$(comm -23 <(echo "$expected") <(echo "$failed"$'\n'"$passed" | sort -u))

echo "────────────────────────────────────────"
echo "Expected failures: $(echo "$expected" | grep -c .) registered in $REGISTER"

rc=0
if [ -n "$unexpected_fail" ]; then
    echo ""
    echo "REGRESSION -- failed but not registered:"
    echo "$unexpected_fail" | sed 's/^/  /'
    rc=1
fi
if [ -n "$unexpected_pass" ]; then
    echo ""
    echo "UNEXPECTED PASS -- registered as failing, but passed.  Remove the row"
    echo "from the register in $REGISTER (and say which chapter landed):"
    echo "$unexpected_pass" | sed 's/^/  /'
    rc=1
fi
if [ -n "$not_run" ]; then
    echo ""
    echo "not reached by this run (skipped), so nothing is known about them:"
    echo "$not_run" | sed 's/^/  /'
fi
if [ "$rc" -eq 0 ]; then
    echo "No surprises: every failure is registered, every registered one failed."
fi
echo "────────────────────────────────────────"
exit "$rc"
