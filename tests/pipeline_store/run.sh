#!/usr/bin/env bash
# SPEC: 62_pipelines
# run.sh — spec/62 F62-24/24a: the local step store outlives the process.  Every check runs
# the pipeline in a fresh aiml process; what one run built, the next one reuses.
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
AIML="${AIML:-$ROOT/out/aiml/aiml}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
run() { (cd "$WORK" && env -u AIML_PIPELINE_STORE "$@" AIML_MEM_V3=1 "$AIML" "$ROOT/tests/pipeline_store/stored.aiml" 2>&1); }
KV="$WORK/.aiml_pipeline/store/kv"

echo "── the step store across processes (spec/62 F62-24/24a) ──"
out=$(run); rc=$?
check "first run: everything runs (2×produce, 2×consume, 2×always)" '[ $rc -eq 0 ] && echo "$out" | grep -q "ran 6 reused 0"'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "four entries in .aiml_pipeline/store/kv, the opaque step none (§7)" '[ "$(ls "$KV" 2>/dev/null | wc -l)" -eq 4 ]'
check "no half-written entry is left" '[ -z "$(ls -a "$KV" | grep -i "tmp")" ]'
out=$(run); rc=$?
check "a second process reuses all four; only the opaque step runs (both lanes)" '[ $rc -eq 0 ] && echo "$out" | grep -q "ran 2 reused 4"'
out=$(run AIML_PIPELINE_STORE="$WORK/other"); rc=$?
check "AIML_PIPELINE_STORE names another store: nothing to reuse there" \
  '[ $rc -eq 0 ] && echo "$out" | grep -q "ran 6 reused 0" && [ "$(ls "$WORK/other/kv" | wc -l)" -eq 4 ]'
out=$(run STORE_FOR_VALUE="$WORK/by_value"); rc=$?
check "pipe.set_store names it for one pipeline value" \
  '[ $rc -eq 0 ] && echo "$out" | grep -q "ran 6 reused 0" && [ "$(ls "$WORK/by_value/kv" | wc -l)" -eq 4 ]'
one=$(ls "$KV" | head -1); printf 'garbage' > "$KV/$one"
out=$(run); rc=$?
check "an unreadable entry is a miss with a warning, never an error or a hit" \
  '[ $rc -eq 0 ] && echo "$out" | grep -q "reused 3" && echo "$out" | grep -qi "unreadable"'
check "and the step that ran again wrote it anew" 'head -c 16 "$KV/$one" | grep -q "aiml-step-entry"'
rm -rf "$WORK/.aiml_pipeline/store"
out=$(run); rc=$?
check "deleting the store is the reset" '[ $rc -eq 0 ] && echo "$out" | grep -q "ran 6 reused 0"'

[ $fail -eq 0 ] && echo "pipeline_store: PASS" || echo "pipeline_store: FAIL"
exit $fail
