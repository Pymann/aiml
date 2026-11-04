#!/usr/bin/env bash
# run.sh — spec/45 "Cost of index maintenance" + spec/31 §9: `kv.set` ist O(1) amortisiert,
# EINSCHLIESSLICH des db_indexed-Hooks.  Die Probe schreibt Bloecke gleicher Groesse in
# denselben wachsenden Speicher und urteilt NORMIERT: ein indiziertes Feld mit wenigen
# verschiedenen Werten (ein Bucket waechst auf die Zahl der Zeilen) gegen eines mit lauter
# verschiedenen Werten (Bucket = 1), im selben Block.  Damit entscheidet das Verhaeltnis,
# nicht die Maschine — wichtig, weil hier Zeit gemessen wird.
#
# Eigenes Verzeichnis und ein Treiber, weil die Probe Zeit braucht (~30 s) und einen
# WAL-Pfad aufraeumen muss.  Stand vor dem Fix (2026-11-04): 4608 ms gegen 48 ms je 4000
# set bei 24k Zeilen, beim Ueberschreiben 13342 ms — danach flach bei ~47 ms.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml-Projektwurzel
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_set_scaling"
WAL="${TMPDIR:-/tmp}/aiml_test_kvscal"
PROBE=tests/kv_set_scaling/probe_set_scaling.aiml

echo "── kv.set: O(1) amortisiert, auch mit db_indexed ──"
rm -rf "$WAL"
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$PROBE" -r 2>&1); rc=$?
echo "$out" | grep -E "^MESS" | sed 's/^/  /'
rm -rf "$WAL" "$CACHE"

if [ $rc -ge 128 ]; then
    printf "  FAIL stuerzte mit rc=%d ab (Signal %d)\n" "$rc" "$((rc-128))"
    echo "── kv_set_scaling FAILED ──"; exit 1
fi
if [ $rc -ne 0 ]; then
    echo "$out" | grep -E "FAIL|error" | head -6 | sed 's/^/  /'
    echo "── kv_set_scaling FAILED ──"; exit 1
fi
echo "  $(echo "$out" | grep -E '^PASS')"
echo "── kv_set_scaling PASS ──"
exit 0
