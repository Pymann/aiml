#!/usr/bin/env bash
# run.sh — every diagnostic has the same form (spec/49, rules F49-100..106).
#
# Two sets, on purpose:
#   strict/  every message MUST carry file, line, column, name and rule
#            reference; a regression here turns the suite red.
#   survey   the rest of the corpus is only COUNTED and reported, so that the state
#            is visible in every run instead of being rediscovered each time.
#
# The probes mark their location themselves with `# <<HERE` -- a
# hand-counted line number in the test rots at the first re-indent.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
DIR=tests/diagnostics

echo "── Diagnoseform (spec/49 F49-100..106) ──"
fail=0
ok()  { printf "  ok   %s\n" "$1"; }
bad() { printf "  FAIL %s\n" "$1"; fail=1; }

for probe in "$DIR"/strict/*.aiml; do
    base=$(basename "$probe")
    want_line=$(grep -n '# <<HERE' "$probe" | head -1 | cut -d: -f1)
    want_txt=$(grep -m1 -oE '# EXPECT: .*' "$probe" | sed 's/# EXPECT: //')
    recs=$("$AIML" "$probe" --check 2>/dev/null | grep -E "^(error|warning)	" || true)
    n=$(printf '%s' "$recs" | grep -c . || true)

    if [ "$n" -ne 1 ]; then
        bad "[$base] $n messages instead of exactly one"
        printf '%s\n' "$recs" | head -3 | sed 's/^/        /'
        continue
    fi

    IFS=$'\t' read -r sev file line col origin msg <<<"$recs"

    [ "$file" = "$probe" ]  && ok "[$base] file named" \
                            || bad "[$base] file='$file' instead of '$probe' (F49-100)"
    [ "$line" = "$want_line" ] && ok "[$base] line $line is the location" \
                               || bad "[$base] line=$line, location is $want_line (F49-100/103)"
    [ "${col:-0}" -gt 0 ]   && ok "[$base] column $col" \
                            || bad "[$base] column=0 — the YAML mark knows it (F49-100)"
    case "$msg" in
        *"$want_txt"*) ok "[$base] message names the finding" ;;
        *) bad "[$base] message without '$want_txt': $msg" ;;
    esac
    # F49-102 requires the name of the thing at issue.  A syntax error
    # has none -- probes with `# NO-NAME` say so and give the reason.
    if grep -q '^# NO-NAME' "$probe"; then
        ok "[$base] without symbol name, justified in the probe"
    else
        case "$msg" in
            *"'"*"'"*) ok "[$base] name in quotes (F49-102)" ;;
            *) bad "[$base] message names no name (F49-102): $msg" ;;
        esac
    fi
    # F49-104 applies to CHECKS.  A syntax error does not enforce a rule,
    # it states that the form is broken -- probes with `# NO-RULE`
    # are exempt, and the exemption is in the probe, not here.
    if grep -q '^# NO-RULE' "$probe"; then
        ok "[$base] without rule reference, justified in the probe"
    else
        case "$msg" in
            *"(spec/"*) ok "[$base] Regelverweis (F49-104)" ;;
            *) bad "[$base] no (spec/...) reference (F49-104): $msg" ;;
        esac
    fi
    case "$msg" in
        *"	"*) bad "[$base] message contains a tab (F49-105)" ;;
        *) ok "[$base] single line, no tab (F49-105)" ;;
    esac
    case "$msg" in
        line\ [0-9]*|*"line "[0-9]*:*) bad "[$base] location is written as prose in the message (F49-100)" ;;
        *) : ;;
    esac
done

# ── survey: the state in the rest of the corpus, only counted ───────────────
echo "── coverage in the rest of the corpus (measurement only, no verdict) ──"
tot=0; with=0
for probe in $(ls tests/*neg*.aiml tests/*/neg_*.aiml tests/*/n[0-9][0-9]_*.aiml 2>/dev/null | head -60); do
    while IFS=$'\t' read -r sev file line col origin msg; do
        [ -z "${sev:-}" ] && continue
        tot=$((tot+1))
        case "${line:-}" in ""|*[!0-9]*) line=0 ;; esac
        if [ "$file" != "-" ] && [ "$line" -gt 0 ]; then with=$((with+1)); fi
    done < <("$AIML" "$probe" --check 2>/dev/null | grep -E "^(error|warning)	" || true)
done
if [ "$tot" -gt 0 ]; then
    printf "  %d of %d messages carry file and line (%d %%)\n" "$with" "$tot" $((with*100/tot))
else
    printf "  no messages collected\n"
fi

[ $fail -eq 0 ] && echo "── diagnostics PASS ──" || echo "── diagnostics FAIL ──"
exit $fail
