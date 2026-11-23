#!/usr/bin/env bash
# run.sh — spec/45 "Cost of index maintenance" + spec/31 §9: `kv.set` is O(1) amortised,
# INCLUDING the db_indexed hook.  The probe writes blocks of equal size into
# the same growing store and judges NORMALISED: an indexed field with few
# distinct values (one bucket grows to the number of rows) against one with all
# distinct values (bucket = 1), in the same block.  So the ratio decides,
# not the machine — important, because time is measured here.
#
# Own directory and a driver, because the probe takes time (~30 s) and has to clean up a
# WAL path.  State before the fix (2026-11-04): 4608 ms vs. 48 ms per 4000
# sets at 24k rows, 13342 ms when overwriting — afterwards flat at ~47 ms.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_set_scaling"
WAL="${TMPDIR:-/tmp}/aiml_test_kvscal"
PROBE=tests/kv_set_scaling/probe_set_scaling.aiml

echo "── kv.set: O(1) amortised, also with db_indexed ──"
rm -rf "$WAL"
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$PROBE" -r 2>&1); rc=$?
echo "$out" | grep -E "^MESS" | sed 's/^/  /'
rm -rf "$WAL" "$CACHE"

if [ $rc -ge 128 ]; then
    printf "  FAIL crashed with rc=%d (signal %d)\n" "$rc" "$((rc-128))"
    echo "── kv_set_scaling FAILED ──"; exit 1
fi
if [ $rc -ne 0 ]; then
    echo "$out" | grep -E "FAIL|error" | head -6 | sed 's/^/  /'
    echo "── kv_set_scaling FAILED ──"; exit 1
fi
echo "  $(echo "$out" | grep -E '^PASS')"
echo "── kv_set_scaling PASS ──"
exit 0
