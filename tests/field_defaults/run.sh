#!/usr/bin/env bash
# run.sh — spec/15 F15-40: ein `list<T>`-Feld nimmt nur `[]` als Default; ein
# GEFUELLTER Listen-Default ist ein Compile-Fehler.
#
# Der Punkt dieses Verzeichnisses ist die MELDUNG.  Der Check gab es nur fuer
# `- model:`/`fields:`; bei `- class:`/`members:` wurde der Default still
# verworfen, das Feld existierte und war zur Laufzeit leer.  Eine leere Liste
# sieht wie ein Datenproblem aus, nicht wie ein Uebersetzungsfehler — in cai
# kostete das drei rote Tests und eine Probe, die `.len` ausgibt.
#
# Jeder neg/-Fall muss mit seinem `# EXPECT-ERROR:`-Text UND einer Fundstelle
# abgewiesen werden; die ok-Faelle muessen weiter uebersetzen und laufen.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml-Projektwurzel
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/field_defaults"

echo "── Feld-Defaults: gefuellter Listen-Default ist ein Fehler (spec/15 F15-40) ──"
fail=0

for f in tests/field_defaults/neg_*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$f" -o /dev/null 2>&1); rc=$?
    if [ $rc -ge 128 ]; then
        printf "  FAIL [%s] stuerzte mit rc=%d ab statt zu melden\n" "$base" "$rc"; fail=1; continue
    fi
    if [ $rc -eq 0 ]; then
        printf "  FAIL [%s] uebersetzte, erwartet war eine Abweisung\n" "$base"; fail=1; continue
    fi
    if ! echo "$out" | grep -qF "$want"; then
        printf "  FAIL [%s] Meldung enthaelt '%s' nicht\n" "$base" "$want"
        echo "$out" | head -4 | sed 's/^/        /'
        fail=1; continue
    fi
    # Die Fundstelle ist der halbe Wert der Meldung: Datei:Zeile muss dastehen.
    if ! echo "$out" | grep -qE "$base:[0-9]+"; then
        printf "  FAIL [%s] Meldung nennt keine Fundstelle\n" "$base"
        echo "$out" | grep -iE 'error|list default' | head -2 | sed 's/^/        /'
        fail=1; continue
    fi
    printf "  ok   [%s] abgewiesen, mit Fundstelle\n" "$base"
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
