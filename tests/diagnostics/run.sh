#!/usr/bin/env bash
# run.sh — jede Diagnose hat dieselbe Form (spec/49, Regeln F49-100..106).
#
# Zwei Mengen, mit Absicht:
#   strict/  jede Meldung MUSS Datei, Zeile, Spalte, Namen und Regelverweis
#            tragen; ein Rueckschritt hier macht die Suite rot.
#   survey   der Rest des Korpus wird nur GEZAEHLT und gemeldet, damit der Stand
#            in jedem Lauf sichtbar ist statt jedes Mal neu entdeckt zu werden.
#
# Die Proben markieren ihre Fundstelle mit `# <<HERE` selbst -- eine
# handgezaehlte Zeilennummer im Test verrottet beim ersten Einruecken.
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
        bad "[$base] $n Meldungen statt genau einer"
        printf '%s\n' "$recs" | head -3 | sed 's/^/        /'
        continue
    fi

    IFS=$'\t' read -r sev file line col origin msg <<<"$recs"

    [ "$file" = "$probe" ]  && ok "[$base] Datei genannt" \
                            || bad "[$base] Datei='$file' statt '$probe' (F49-100)"
    [ "$line" = "$want_line" ] && ok "[$base] Zeile $line ist die Fundstelle" \
                               || bad "[$base] Zeile=$line, Fundstelle ist $want_line (F49-100/103)"
    [ "${col:-0}" -gt 0 ]   && ok "[$base] Spalte $col" \
                            || bad "[$base] Spalte=0 — die YAML-Marke kennt sie (F49-100)"
    case "$msg" in
        *"$want_txt"*) ok "[$base] Meldung nennt den Befund" ;;
        *) bad "[$base] Meldung ohne '$want_txt': $msg" ;;
    esac
    # F49-102 verlangt den Namen des Dings, um das es geht.  Ein Syntaxfehler
    # hat keines -- Proben mit `# NO-NAME` sagen das und begruenden es.
    if grep -q '^# NO-NAME' "$probe"; then
        ok "[$base] ohne Symbolnamen, begruendet in der Probe"
    else
        case "$msg" in
            *"'"*"'"*) ok "[$base] Name in Anfuehrungszeichen (F49-102)" ;;
            *) bad "[$base] Meldung nennt keinen Namen (F49-102): $msg" ;;
        esac
    fi
    # F49-104 gilt fuer CHECKS.  Ein Syntaxfehler setzt keine Regel durch,
    # sondern stellt fest, dass die Form gebrochen ist -- Proben mit `# NO-RULE`
    # sind davon ausgenommen, und die Ausnahme steht in der Probe, nicht hier.
    if grep -q '^# NO-RULE' "$probe"; then
        ok "[$base] ohne Regelverweis, begruendet in der Probe"
    else
        case "$msg" in
            *"(spec/"*) ok "[$base] Regelverweis (F49-104)" ;;
            *) bad "[$base] kein (spec/...)-Verweis (F49-104): $msg" ;;
        esac
    fi
    case "$msg" in
        *"	"*) bad "[$base] Meldung enthaelt einen Tabulator (F49-105)" ;;
        *) ok "[$base] einzeilig, kein Tabulator (F49-105)" ;;
    esac
    case "$msg" in
        line\ [0-9]*|*"line "[0-9]*:*) bad "[$base] Ort steht als Prosa in der Meldung (F49-100)" ;;
        *) : ;;
    esac
done

# ── survey: der Stand im uebrigen Korpus, nur gezaehlt ───────────────────────
echo "── Deckung im uebrigen Korpus (nur Messung, kein Urteil) ──"
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
    printf "  %d von %d Meldungen tragen Datei und Zeile (%d %%)\n" "$with" "$tot" $((with*100/tot))
else
    printf "  keine Meldungen eingesammelt\n"
fi

[ $fail -eq 0 ] && echo "── diagnostics PASS ──" || echo "── diagnostics FAIL ──"
exit $fail
