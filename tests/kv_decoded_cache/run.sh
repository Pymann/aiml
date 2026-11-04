#!/usr/bin/env bash
# run.sh — spec/31: der decoded_cache eines typisierten Kv ist auf 1024 Eintraege begrenzt,
# und ein Lesen liefert nie einen Slot, den dieselbe Verdraengung schon freigegeben hat.
#
# Zwei Prozesse ueber dieselbe WAL: der erste legt 1200 Zeilen an (mehr als das
# Cache-Fenster), der zweite oeffnet sie wieder.  Beim Oeffnen fuellt der Backfill der
# db_indexed-Felder den Cache mit allen 1200 Zeilen; danach schlaegt beim ersten
# Cache-Miss die Verdraengung zu.
#
# Das ist der cai-Absturz vom 2026-11-04 (SIGSEGV in __strlen_avx2 <- enc <-
# aiml_ser_deep_copy <- cai.wissen.Aufloesung_eintraege) in klein.  rc >= 128 ist
# deshalb ein eigener Fehlerfall: ein Absturz ist hier das erwartete FALSCHE Verhalten.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml-Projektwurzel
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_decoded_cache"
WAL="${TMPDIR:-/tmp}/aiml_test_kvcache"
PROBE=tests/kv_decoded_cache/probe_reopen_read.aiml

echo "── Kv decoded_cache: begrenzt, und ein Lesen gibt keinen freigegebenen Slot zurueck ──"
rm -rf "$WAL"
fail=0

lauf() {   # $1 = Beschriftung
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$PROBE" -r 2>&1); rc=$?
    if [ $rc -ge 128 ]; then
        printf "  FAIL [%s] stuerzte mit rc=%d ab (Signal %d)\n" "$1" "$rc" "$((rc-128))"
        echo "$out" | grep -iE "crash|signal|backtrace" | head -3 | sed 's/^/        /'
        fail=1; return 1
    fi
    if [ $rc -ne 0 ]; then
        printf "  FAIL [%s] rc=%d\n" "$1" "$rc"
        echo "$out" | grep -E "FAIL|error" | head -4 | sed 's/^/        /'
        fail=1; return 1
    fi
    printf "  ok   [%s] %s\n" "$1" "$(echo "$out" | grep -E 'SEED|PASS' | head -1)"
    return 0
}

lauf "1. Lauf: anlegen" && lauf "2. Lauf: wieder oeffnen und lesen"

rm -rf "$WAL" "$CACHE"
if [ $fail -ne 0 ]; then echo "── kv_decoded_cache FAILED ──"; exit 1; fi
echo "── kv_decoded_cache PASS ──"
exit 0
