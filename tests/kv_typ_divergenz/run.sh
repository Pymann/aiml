#!/usr/bin/env bash
# run.sh — spec/31 §6.10: the typing of a store belongs to the store.
#
# Two cases over the same registry:
#   1. the same store twice with THE SAME type (global + class member) → holds
#   2. the same store twice with a DIFFERENT type → loud message + abort
#
# Case 2 was the silent path: `has` said 1, the record looked plausible, and when
# passed on the process died in aiml_ser_encode <- aiml_ser_deep_copy
# (datacreatures, aborted repository pilot of 11.11.2026).  A crash
# (rc >= 128) is therefore a failure case of its own here -- rc=1 is expected.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_typ_divergenz"
DB=/tmp/aiml_test_kvtypdiv

echo "── Kv: one store, one value type (spec/31 §6.10) ──"
rm -rf "$DB"
fail=0

# ── case 1: same type, two openers ───────────────────────────────────────
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 AIML_LINT_GLOBALS=0 \
      "$AIML" tests/kv_typ_divergenz/probe_gleich.aiml -r 2>&1); rc=$?
if [ $rc -ge 128 ]; then
    printf "  FAIL [same type] crashed with rc=%d (signal %d)\n" "$rc" "$((rc-128))"; fail=1
elif [ $rc -ne 0 ] || ! echo "$out" | grep -q "PASS same type"; then
    printf "  FAIL [same type] rc=%d\n" "$rc"
    echo "$out" | grep -E "FAIL|FATAL|error" | head -4 | sed 's/^/        /'; fail=1
else
    printf "  ok   [same type] %s\n" "$(echo "$out" | grep -o "PASS same type.*" | head -1)"
fi

# ── case 2: different type → loud + abort ────────────────────────────────
out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 AIML_LINT_GLOBALS=0 \
      "$AIML" tests/kv_typ_divergenz/probe_kreuz.aiml -r 2>&1); rc=$?
if [ $rc -ge 128 ]; then
    printf "  FAIL [foreign type] crashed with rc=%d (signal %d) instead of reporting\n" "$rc" "$((rc-128))"
    echo "$out" | grep -iE "crash|signal" | head -2 | sed 's/^/        /'; fail=1
elif echo "$out" | grep -q "FAIL: second open was served silently"; then
    printf "  FAIL [foreign type] was served SILENTLY — the guard is missing\n"; fail=1
elif [ $rc -eq 0 ]; then
    printf "  FAIL [foreign type] rc=0 — an abort was expected\n"; fail=1
elif ! echo "$out" | grep -q "6.10"; then
    printf "  FAIL [foreign type] aborts (rc=%d) but does not name spec/31 §6.10\n" "$rc"
    echo "$out" | tail -3 | sed 's/^/        /'; fail=1
else
    printf "  ok   [foreign type] %s\n" "$(echo "$out" | grep -o "\[AionDB\] FATAL: KV store.*" | head -1 | cut -c1-110)"
fi

rm -rf "$DB" "$CACHE"
if [ $fail -ne 0 ]; then echo "── kv_typ_divergenz FAILED ──"; exit 1; fi
echo "── kv_typ_divergenz PASS ──"
exit 0
