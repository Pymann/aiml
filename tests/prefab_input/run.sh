#!/usr/bin/env bash
# run.sh — tier 4: input reachability (spec/44_8, F44_8-1..6).
#
# What is checked is the DIAGNOSTIC of `--check`, not a program run: the cases
# have no windows and no mouse.  Every neg_ probe must yield EXACTLY one finding
# of its rule, at the line marked with `# <<HERE` and with the
# expected severity.  Every ok_ probe is the rule's escape hatch and must
# stay silent -- without this half the other proves nothing.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
# F44_8-5 and F44_8-6 are hints behind a switch (spec/44_8): their
# evidence rests on the tier-2 binding, which has gaps in real code.  The
# driver checks them anyway -- but switched on, the way they are meant.
export AIML_LINT_PREFAB_INPUT=1
DIR=tests/prefab_input

echo "── Eingabe-Erreichbarkeit (spec/44_8 Tier 4) ──"
fail=0
ok()  { printf "  ok   %s\n" "$1"; }
bad() { printf "  FAIL %s\n" "$1"; fail=1; }

for probe in "$DIR"/neg_*.aiml; do
    base=$(basename "$probe")
    rule=$(grep -m1 -oE '# EXPECT: .*'     "$probe" | sed 's/# EXPECT: //')
    want_sev=$(grep -m1 -oE '# EXPECT-SEV: .*' "$probe" | sed 's/# EXPECT-SEV: //')
    want_line=$(grep -n '# <<HERE' "$probe" | head -1 | cut -d: -f1)

    recs=$("$AIML" "$probe" --check 2>/dev/null | grep -F "$rule" || true)
    n=$(printf '%s' "$recs" | grep -c . || true)
    if [ "$n" -ne 1 ]; then
        bad "[$base] $n findings for $rule instead of exactly one"
        "$AIML" "$probe" --check 2>/dev/null | cut -c1-120 | sed 's/^/        /'
        continue
    fi
    IFS=$'\t' read -r sev file line col origin msg <<<"$recs"
    [ "$sev" = "$want_sev" ] && ok "[$base] $rule as $sev" \
                             || bad "[$base] $rule as '$sev', expected '$want_sev'"
    [ "$file" = "$probe" ]   && ok "[$base] file named" \
                             || bad "[$base] file='$file'"
    [ "$line" = "$want_line" ] && ok "[$base] line $line is the location" \
                               || bad "[$base] line=$line, location is $want_line"
    case "$msg" in
        *"'"*"'"*) ok "[$base] names the name" ;;
        *) bad "[$base] names no name: $msg" ;;
    esac
done

# The escape hatches must stay silent.
for probe in "$DIR"/ok_*.aiml; do
    base=$(basename "$probe")
    why=$(grep -m1 -oE '# EXPECT-SILENT: .*' "$probe" | sed 's/# EXPECT-SILENT: //')
    hits=$("$AIML" "$probe" --check 2>/dev/null | grep -cF "F44_8-" || true)
    [ "$hits" -eq 0 ] && ok "[$base] silent — $why" \
                      || { bad "[$base] $hits finding(s), although: $why"
                           "$AIML" "$probe" --check 2>/dev/null | grep -F "F44_8-" | cut -c1-120 | sed 's/^/        /'; }
done

[ $fail -eq 0 ] && echo "── prefab_input PASS ──" || echo "── prefab_input FAIL ──"
exit $fail
