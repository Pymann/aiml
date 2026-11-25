#!/usr/bin/env bash
# tools/check_expected_failures.sh is what makes the corpus gateable, so it needs
# its own proof -- above all that it FAILS when it should.  A gate that cannot go
# red is decoration.
#
# Fixtures only: no compiler, no display, nothing from the real corpus.
set -u
cd "$(dirname "$0")/../.."
CHECK=tools/check_expected_failures.sh
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0

reg() {   # a register fixture with the given entries
    { echo "<!-- expected-failures:begin -->"
      echo "| Test | Why |"
      echo "|------|-----|"
      for t in "$@"; do echo "| \`$t\` | because |"; done
      echo "<!-- expected-failures:end -->"
      echo "| \`tests/outside_the_block.aiml\` | must be ignored |"
    } > "$TMP/reg.md"
}
res() { : > "$TMP/res.txt"; for l in "$@"; do echo "$l" >> "$TMP/res.txt"; done; }

check() {  # name, expected-rc
    local name="$1" want="$2" got out
    out=$("$CHECK" "$TMP/res.txt" "$TMP/reg.md" 2>&1); got=$?
    if [ "$got" = "$want" ]; then
        printf "  ok   %-44s rc=%s\n" "$name" "$got"; pass=$((pass+1))
    else
        printf "  FAIL %-44s rc=%s, wanted %s\n" "$name" "$got" "$want"
        echo "$out" | sed 's/^/       /'; fail=$((fail+1))
    fi
}
has() {    # name, needle
    local name="$1" needle="$2" out
    out=$("$CHECK" "$TMP/res.txt" "$TMP/reg.md" 2>&1)
    if echo "$out" | grep -q "$needle"; then
        printf "  ok   %-44s says '%s'\n" "$name" "$needle"; pass=$((pass+1))
    else
        printf "  FAIL %-44s never says '%s'\n" "$name" "$needle"
        echo "$out" | sed 's/^/       /'; fail=$((fail+1))
    fi
}

echo "── tools/check_expected_failures.sh ──"

reg tests/a.aiml tests/b/run.sh
res "[ FAIL ] tests/a.aiml (Expected: 0, Got: 1)" \
    "[ FAIL ] tests/b/run.sh (Exit Code: 1)" \
    "[  OK  ] tests/c.aiml (Exit Code: 0)"
check "exactly the registered ones fail" 0

res "[ FAIL ] tests/a.aiml (Expected: 0, Got: 1)" \
    "[ FAIL ] tests/b/run.sh (Exit Code: 1)" \
    "[ FAIL ] tests/regressed.aiml (Expected: 0, Got: 1)"
check "an unregistered failure is a regression" 1
has   "and it is named" "tests/regressed.aiml"

res "[  OK  ] tests/a.aiml (Exit Code: 0)" \
    "[ FAIL ] tests/b/run.sh (Exit Code: 1)"
check "a registered test that passes fails too" 1
has   "and it asks for the row to go" "UNEXPECTED PASS"

res "[ FAIL ] tests/b/run.sh (Exit Code: 1)" \
    "[ SKIP ] tests/a.aiml (Requires: display)"
check "a skipped registered test is not a failure" 0
has   "but it is reported" "not reached by this run"

# A row outside the markers must not count, or the register would quietly grow
# by every backtick anywhere in the document.
res "[ FAIL ] tests/a.aiml (x)" "[ FAIL ] tests/b/run.sh (x)" \
    "[ FAIL ] tests/outside_the_block.aiml (x)"
check "rows outside the markers do not count" 1

# No markers at all must be an error, not an empty register that passes everything.
printf '%s\n' "| \`tests/a.aiml\` | no markers |" > "$TMP/reg.md"
res "[ FAIL ] tests/anything.aiml (x)"
check "a register without markers is an error" 2

echo "── $pass ok, $fail failed ──"
[ "$fail" -eq 0 ]
