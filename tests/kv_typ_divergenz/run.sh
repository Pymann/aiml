#!/usr/bin/env bash
# run.sh — spec/31 §6.10: die Typisierung eines Stores gehoert dem Store.
#
# Zwei Faelle ueber dieselbe Registratur:
#   1. derselbe Store zweimal mit DEMSELBEN Typ (Global + Klassenmitglied) → traegt
#   2. derselbe Store zweimal mit VERSCHIEDENEM Typ → laute Meldung + Abbruch
#
# Fall 2 war der stille Pfad: `has` sagte 1, der Satz sah plausibel aus, und beim
# Weitergeben starb der Prozess in aiml_ser_encode <- aiml_ser_deep_copy
# (datacreatures, abgebrochener Repository-Pilot vom 11.11.2026).  Ein Absturz
# (rc >= 128) ist hier deshalb ein eigener Fehlerfall -- erwartet ist rc=1.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml-Projektwurzel
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_typ_divergenz"
DB=/tmp/aiml_test_kvtypdiv

echo "── Kv: ein Store, ein Werttyp (spec/31 §6.10) ──"
rm -rf "$DB"
fail=0

# ── Fall 1: gleicher Typ, zwei Oeffner ─────────────────────────────────────
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 AIML_LINT_GLOBALS=0 \
      "$AIML" tests/kv_typ_divergenz/probe_gleich.aiml -r 2>&1); rc=$?
if [ $rc -ge 128 ]; then
    printf "  FAIL [gleicher Typ] stuerzte mit rc=%d ab (Signal %d)\n" "$rc" "$((rc-128))"; fail=1
elif [ $rc -ne 0 ] || ! echo "$out" | grep -q "PASS same type"; then
    printf "  FAIL [gleicher Typ] rc=%d\n" "$rc"
    echo "$out" | grep -E "FAIL|FATAL|error" | head -4 | sed 's/^/        /'; fail=1
else
    printf "  ok   [gleicher Typ] %s\n" "$(echo "$out" | grep -o "PASS same type.*" | head -1)"
fi

# ── Fall 2: verschiedener Typ → laut + Abbruch ─────────────────────────────
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 AIML_LINT_GLOBALS=0 \
      "$AIML" tests/kv_typ_divergenz/probe_kreuz.aiml -r 2>&1); rc=$?
if [ $rc -ge 128 ]; then
    printf "  FAIL [fremder Typ] stuerzte mit rc=%d ab (Signal %d) statt zu melden\n" "$rc" "$((rc-128))"
    echo "$out" | grep -iE "crash|signal" | head -2 | sed 's/^/        /'; fail=1
elif echo "$out" | grep -q "FAIL: second open was served silently"; then
    printf "  FAIL [fremder Typ] wurde STILL bedient — die Wache fehlt\n"; fail=1
elif [ $rc -eq 0 ]; then
    printf "  FAIL [fremder Typ] rc=0 — erwartet war ein Abbruch\n"; fail=1
elif ! echo "$out" | grep -q "6.10"; then
    printf "  FAIL [fremder Typ] bricht ab (rc=%d), nennt aber spec/31 §6.10 nicht\n" "$rc"
    echo "$out" | tail -3 | sed 's/^/        /'; fail=1
else
    printf "  ok   [fremder Typ] %s\n" "$(echo "$out" | grep -o "\[AionDB\] FATAL: KV store.*" | head -1 | cut -c1-110)"
fi

rm -rf "$DB" "$CACHE"
if [ $fail -ne 0 ]; then echo "── kv_typ_divergenz FAILED ──"; exit 1; fi
echo "── kv_typ_divergenz PASS ──"
exit 0
