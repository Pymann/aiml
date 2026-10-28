#!/usr/bin/env bash
# run.sh — spec/58 §12 group interfaces: positive tests pass, every negative test
# fails with EXACTLY its `# EXPECT-ERROR:` message (exit code 1 alone proves
# nothing — the file could fail for an unrelated reason).
#   bash tests/classgroup_giface/run.sh            (VALGRIND=1: positives under memcheck)
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
fail=0; pass=0
ok()  { pass=$((pass+1)); printf "  ok   %s\n" "$1"; }
bad() { fail=$((fail+1)); printf "  FAIL %s\n" "$1"; }
echo "── group interfaces (spec/58 §12) ──"
for f in tests/test_classgroup_giface.aiml tests/test_classgroup_giface_instance_impl.aiml tests/test_classgroup_giface_cross_unit.aiml; do
  out=$(env AIML_MEM_V3=1 timeout 120 "$AIML" "$f" 2>&1); rc=$?
  if [ $rc -eq 0 ] && echo "$out" | grep -q "^PASS"; then ok "$(basename "$f")"; else bad "$(basename "$f") rc=$rc $(echo "$out" | grep -E 'FAIL|ERROR' | head -2 | tr '\n' ' ')"; fi
  if [ "${VALGRIND:-0}" = "1" ]; then
    # the usual procedure (tests/run_memory_sanitizers.sh): memory SAFETY fails the run,
    # leaks are reported (the JIT itself has a baseline) but do not count.
    vg=$(env AIML_MEM_V3=1 AIML_LIFETIME_DROPS=1 valgrind --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=none --error-exitcode=99 "$AIML" -r "$f" 2>&1); vrc=$?
    if [ $vrc -ne 99 ] && ! echo "$vg" | grep -qE 'Invalid free|Invalid read|Invalid write|Process terminating with default action of signal'; then
      ok "valgrind $(basename "$f") ($(echo "$vg" | grep -oE 'definitely lost: [0-9,]+ bytes' | head -1))"
    else bad "valgrind $(basename "$f") ($(echo "$vg" | grep -E 'Invalid|signal' | head -2 | tr '\n' ' '))"; fi
  fi
done
for f in tests/test_classgroup_neg_giface_*.aiml; do
  want=$(grep -m1 '^# EXPECT-ERROR: ' "$f" | sed 's/^# EXPECT-ERROR: //')
  out=$(env AIML_MEM_V3=1 timeout 120 "$AIML" "$f" 2>&1); rc=$?
  if [ $rc -ne 0 ] && echo "$out" | grep -qF -- "$want"; then ok "$(basename "$f")"
  else bad "$(basename "$f") rc=$rc — want: $want — got: $(echo "$out" | grep ERROR | head -1)"; fi
done
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
