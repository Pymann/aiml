#!/usr/bin/env bash
# run_lifetime.sh — run the lifetime lab probes against the spec/61 target model.
#
#   bash tests/lifetime/run_lifetime.sh              # spec61 target (default) — RED until the
#                                                    #   compiler implements spec/61 (expected)
#   MODE=off  bash tests/lifetime/run_lifetime.sh    # old drops-on model, for model-agnostic probes
#   MODE=v2   bash tests/lifetime/run_lifetime.sh    # old v2 refcount-everything model
#   TRACE=2   bash tests/lifetime/run_lifetime.sh    # + AIML_MODEL_TRACE ring / UAF reports
#   ONLY=03   bash tests/lifetime/run_lifetime.sh    # run only probes whose name matches
#
# Exit 0 iff every probe returns 0. Under the default (spec61) that will NOT hold until the
# compiler rewrite lands — the probes are the acceptance target (see README).
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
TRACE="${TRACE:-0}"
MODE="${MODE:-spec61}"
ONLY="${ONLY:-}"

env_common=(AIML_MODEL_TRACE="$TRACE")
case "$MODE" in
  spec61) env_common+=(AIML_LIFETIME_DROPS=1 AIML_MEM_V3=1) ;;   # spec/61 target model (v3 gate)
  off)    env_common+=(AIML_LIFETIME_DROPS=1 AIML_MEM_V3=0) ;;    # old drops-on / value model (memV3 is now the compiler default → disable explicitly)
  v2)     env_common+=(AIML_LIFETIME_DROPS=1 AIML_MEM_V3=0 AIML_MODEL_REFCOUNT=1) ;;  # old refcount-everything
  *) echo "unknown MODE=$MODE (use spec61|off|v2)"; exit 2 ;;
esac

echo "── lifetime lab — MODE=$MODE  (spec61 = target; expect RED until the compiler rewrite) ──"
fail=0; pass=0
for f in tests/lifetime/[0-9]*.aiml; do
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  tier=$(grep -m1 -oE '# TIER: T[0-2]' "$f" | grep -oE 'T[0-2]' || echo "--")
  out=$(env "${env_common[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  line=$(echo "$out" | grep -E "PASS|FAIL|use-after-free|error:" | head -1)
  if [ $rc -eq 0 ]; then pass=$((pass+1)); printf "  ok   [%s] %-34s %s\n" "$tier" "$base" "$line"
  else fail=$((fail+1)); printf "  FAIL [%s] %-34s rc=%-3d %s\n" "$tier" "$base" "$rc" "$line"; fi
done
echo "── $pass passed, $fail failed (MODE=$MODE) ──"

# ── Negative probes (§10 diagnostics): each MUST be REJECTED at compile time with a message
# matching its `# EXPECT-ERROR:` header. Only meaningful under spec61 (memV3 gates the checks).
if [ "$MODE" = "spec61" ] && [ -d tests/lifetime/neg ]; then
  npass=0; nfail=0
  for f in tests/lifetime/neg/*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
    want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
    out=$(env "${env_common[@]}" "$AIML" "$f" -r 2>&1); rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -qF "$want"; then
      npass=$((npass+1)); printf "  ok   [NEG] %-30s rejected: %s\n" "$base" "$want"
    else
      nfail=$((nfail+1)); printf "  FAIL [NEG] %-30s rc=%-3d (wanted error containing: %s)\n" "$base" "$rc" "$want"
    fi
  done
  echo "── neg: $npass rejected as expected, $nfail wrong (MODE=$MODE) ──"
  fail=$((fail + nfail))
fi

# ── Warn probes (§10 lints): each MUST compile+run (rc 0) AND emit its `# EXPECT-WARN:` message.
if [ "$MODE" = "spec61" ] && [ -d tests/lifetime/warn ]; then
  wpass=0; wfail=0
  for f in tests/lifetime/warn/*.aiml; do
    [ -e "$f" ] || continue
    base=$(basename "$f")
    [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
    want=$(grep -m1 -oE '# EXPECT-WARN: .*' "$f" | sed 's/# EXPECT-WARN: //')
    out=$(env "${env_common[@]}" "$AIML" "$f" -r 2>&1); rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -qF "$want"; then
      wpass=$((wpass+1)); printf "  ok   [WARN] %-30s warned: %s\n" "$base" "$want"
    else
      wfail=$((wfail+1)); printf "  FAIL [WARN] %-30s rc=%-3d (wanted rc0 + warning: %s)\n" "$base" "$rc" "$want"
    fi
  done
  echo "── warn: $wpass warned as expected, $wfail wrong (MODE=$MODE) ──"
  fail=$((fail + wfail))
fi

[ $fail -eq 0 ]
