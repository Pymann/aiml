#!/usr/bin/env bash
# run.sh — spec/31: the decoded_cache of a typed Kv is limited to 1024 entries,
# and a read never returns a slot that the same eviction has already freed.
#
# Two processes over the same WAL: the first creates 1200 rows (more than the
# cache window), the second opens it again.  On opening, the backfill of the
# db_indexed fields fills the cache with all 1200 rows; after that the eviction
# strikes at the first cache miss.
#
# This is the cai crash of 2026-11-04 (SIGSEGV in __strlen_avx2 <- enc <-
# aiml_ser_deep_copy <- cai.wissen.Aufloesung_eintraege) in miniature.  rc >= 128 is
# therefore a failure case of its own: a crash is the expected WRONG behaviour here.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
CACHE="${AIML_CACHE:-__aiml_cache}/kv_decoded_cache"
WAL="${TMPDIR:-/tmp}/aiml_test_kvcache"
PROBE=tests/kv_decoded_cache/probe_reopen_read.aiml

echo "── Kv decoded_cache: bounded, and a read returns no freed slot ──"
rm -rf "$WAL"
fail=0

lauf() {   # $1 = label
    out=$(AIML_CACHE="$CACHE" AIML_MEM_V3=1 "$AIML" "$PROBE" -r 2>&1); rc=$?
    if [ $rc -ge 128 ]; then
        printf "  FAIL [%s] crashed with rc=%d (signal %d)\n" "$1" "$rc" "$((rc-128))"
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

lauf "run 1: create" && lauf "run 2: reopen and read"

rm -rf "$WAL" "$CACHE"
if [ $fail -ne 0 ]; then echo "── kv_decoded_cache FAILED ──"; exit 1; fi
echo "── kv_decoded_cache PASS ──"
exit 0
