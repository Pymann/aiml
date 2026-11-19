#!/usr/bin/env bash
# run.sh — Tier 4: Eingabe-Erreichbarkeit (spec/44_8, F44_8-1..6).
#
# Geprueft wird die DIAGNOSE von `--check`, nicht ein Programmlauf: die Faelle
# haben keine Fenster und keine Maus.  Jede neg_-Probe muss GENAU einen Befund
# ihrer Regel liefern, an der mit `# <<HERE` markierten Zeile und mit der
# erwarteten Schwere.  Jede ok_-Probe ist der Notausgang der Regel und muss
# schweigen -- ohne diese Haelfte beweist die andere nichts.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
# F44_8-5 und F44_8-6 sind Hinweise hinter einem Schalter (spec/44_8): ihre
# Beweislage ruht auf der Tier-2-Bindung, die in echtem Code Luecken hat.  Der
# Treiber prueft sie trotzdem -- aber eingeschaltet, so wie sie gemeint sind.
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
        bad "[$base] $n Befunde fuer $rule statt genau einem"
        "$AIML" "$probe" --check 2>/dev/null | cut -c1-120 | sed 's/^/        /'
        continue
    fi
    IFS=$'\t' read -r sev file line col origin msg <<<"$recs"
    [ "$sev" = "$want_sev" ] && ok "[$base] $rule als $sev" \
                             || bad "[$base] $rule als '$sev', erwartet '$want_sev'"
    [ "$file" = "$probe" ]   && ok "[$base] Datei genannt" \
                             || bad "[$base] Datei='$file'"
    [ "$line" = "$want_line" ] && ok "[$base] Zeile $line ist die Fundstelle" \
                               || bad "[$base] Zeile=$line, Fundstelle ist $want_line"
    case "$msg" in
        *"'"*"'"*) ok "[$base] nennt den Namen" ;;
        *) bad "[$base] nennt keinen Namen: $msg" ;;
    esac
done

# Die Notausgaenge muessen schweigen.
for probe in "$DIR"/ok_*.aiml; do
    base=$(basename "$probe")
    why=$(grep -m1 -oE '# EXPECT-SILENT: .*' "$probe" | sed 's/# EXPECT-SILENT: //')
    hits=$("$AIML" "$probe" --check 2>/dev/null | grep -cF "F44_8-" || true)
    [ "$hits" -eq 0 ] && ok "[$base] schweigt — $why" \
                      || { bad "[$base] $hits Befund(e), obwohl: $why"
                           "$AIML" "$probe" --check 2>/dev/null | grep -F "F44_8-" | cut -c1-120 | sed 's/^/        /'; }
done

[ $fail -eq 0 ] && echo "── prefab_input PASS ──" || echo "── prefab_input FAIL ──"
exit $fail
