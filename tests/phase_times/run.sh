#!/usr/bin/env bash
# run.sh — die Messschalter aus spec/70 §11.3: --phase-times, --parse-only, --no-cache.
#
# Der Sinn der Schalter ist eine Zahl, auf die sich jemand verlaesst.  Darum pruefen
# die Faelle hier nicht "laeuft durch", sondern die Form: genau die Zaehler aus
# §11.2, ganzzahlige Millisekunden, auf STDERR (stdout gehoert dem Programm), und
# -- das Wichtigste -- dass die Schalter das Ergebnis NICHT veraendern (F70-93).
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml-Projektwurzel
AIML="${AIML:-out/aiml/aiml}"
DIR=tests/phase_times
WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

echo "── Messschalter (spec/70 §11) ──"
fail=0
ok()   { printf "  ok   %s\n" "$1"; }
bad()  { printf "  FAIL %s\n" "$1"; fail=1; }

# ── F70-90: --phase-times nennt genau die Zaehler aus §11.2, auf stderr ──
err="$WORK/pt.err"; out="$WORK/pt.out"
"$AIML" "$DIR/probe.aiml" --phase-times -o "$WORK/probe.ll" > "$out" 2> "$err"
rc=$?
[ $rc -eq 0 ] || bad "--phase-times: rc=$rc"
for want in parse reparse ir emit total; do
    if grep -qE "^phase	$want	[0-9]+$" "$err"; then ok "Zaehler '$want' auf stderr, ganzzahlig"
    else bad "Zaehler '$want' fehlt oder hat die falsche Form"; fi
done
if grep -qE "^phase	" "$out"; then bad "Zaehler landen auf stdout — stdout gehoert dem Programm (F70-90)"
else ok "stdout bleibt frei von Messdaten"; fi
# total darf nicht kleiner sein als die groesste Einzelphase
tot=$(grep -E "^phase	total	" "$err" | cut -f3)
mx=$(grep -E "^phase	(parse|reparse|ir|emit)	" "$err" | cut -f3 | sort -n | tail -1)
if [ -n "$tot" ] && [ -n "$mx" ] && [ "$tot" -ge "$mx" ]; then ok "total >= groesste Einzelphase ($tot >= $mx)"
else bad "total=$tot ist kleiner als die groesste Phase=$mx"; fi

# ── total deckt AUCH den Objektpfad ab (dort lief compileToObject ausserhalb) ──
oerr="$WORK/obj.err"
"$AIML" "$DIR/probe.aiml" --phase-times -o "$WORK/probe.o" > /dev/null 2> "$oerr"
otot=$(grep -E "^phase	total	" "$oerr" | cut -f3)
omx=$(grep -E "^phase	(parse|reparse|ir|emit)	" "$oerr" | cut -f3 | sort -n | tail -1)
if [ -n "$otot" ] && [ -n "$omx" ] && [ "$otot" -ge "$omx" ]; then ok "Objektpfad: total >= groesste Phase ($otot >= $omx)"
else bad "Objektpfad: total=$otot < groesste Phase=$omx — die Objektausgabe faellt aus total heraus"; fi

# ── F70-91: --parse-only erzeugt nichts und laeuft nur bis zum AST ──
rm -f "$WORK/po.ll"
"$AIML" "$DIR/probe.aiml" --parse-only -o "$WORK/po.ll" > "$WORK/po.out" 2> "$WORK/po.err"
rc=$?
[ $rc -eq 0 ] || bad "--parse-only: rc=$rc"
if [ -s "$WORK/po.ll" ]; then bad "--parse-only hat trotzdem IR geschrieben (F70-91)"
else ok "--parse-only schreibt nichts"; fi
"$AIML" "$DIR/probe.aiml" --parse-only --phase-times -o "$WORK/po2.ll" > /dev/null 2> "$WORK/po2.err"
if grep -qE "^phase	(ir|emit)	" "$WORK/po2.err"; then
    bad "--parse-only meldet ir/emit — die Phasen laufen gar nicht (F70-91)"
else ok "--parse-only meldet keine ir/emit-Phase"; fi
if grep -qE "^phase	parse	[0-9]+$" "$WORK/po2.err"; then ok "--parse-only meldet die parse-Phase"
else bad "--parse-only meldet die parse-Phase nicht"; fi

# ── F70-91: ein Frontend-Fehler bleibt ein Fehler ──
want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$DIR/neg_frontend_error.aiml" | sed 's/# EXPECT-ERROR: //')
msg=$("$AIML" "$DIR/neg_frontend_error.aiml" --parse-only -o "$WORK/neg.ll" 2>&1); rc=$?
if [ $rc -eq 0 ]; then bad "--parse-only hat den Frontend-Fehler verschluckt"
elif ! echo "$msg" | grep -qF "$want"; then bad "Meldung ohne '$want'"; echo "$msg" | head -3 | sed 's/^/        /'
else ok "Frontend-Fehler wird auch mit --parse-only gemeldet"; fi

# ── F70-92/93: --no-cache aendert das Ergebnis nicht ──
"$AIML" "$DIR/probe.aiml" -o "$WORK/a.ll" > /dev/null 2>&1
"$AIML" "$DIR/probe.aiml" --no-cache -o "$WORK/b.ll" > /dev/null 2>&1
if [ ! -s "$WORK/a.ll" ] || [ ! -s "$WORK/b.ll" ]; then bad "--no-cache: eine der beiden Ausgaben fehlt"
elif cmp -s "$WORK/a.ll" "$WORK/b.ll"; then ok "--no-cache liefert byteidentische IR (F70-93)"
else bad "--no-cache veraendert die Ausgabe (F70-93)"; fi
if "$AIML" "$DIR/probe.aiml" --no-cache --phase-times -o "$WORK/c.ll" 2>&1 | grep -q "unknown"; then
    bad "--no-cache wird nicht erkannt"
else ok "--no-cache wird angenommen"; fi

[ $fail -eq 0 ] && echo "── phase_times PASS ──" || echo "── phase_times FAIL ──"
exit $fail
