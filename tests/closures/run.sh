#!/usr/bin/env bash
# run.sh — run the block/closure probes against the spec/64 target.
#
#   bash tests/closures/run.sh          # RED until the compiler implements spec/64
#   ONLY=02 bash tests/closures/run.sh  # only probes whose name matches
#   TRACE=2 bash tests/closures/run.sh  # + AIML_MODEL_TRACE ring / UAF reports
#
# Exit 0 iff every positive probe returns 0 AND every neg/ probe is rejected with its
# `# EXPECT-ERROR:` message. The compiler itself is built with `exodus build --all` from the
# exvenv — this script only *runs* the probes with the already-built binary.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
TRACE="${TRACE:-0}"
ONLY="${ONLY:-}"

env_common=(AIML_MODEL_TRACE="$TRACE" AIML_LIFETIME_DROPS=1 AIML_MEM_V3=1)

echo "── block/closure lab (spec/64 target — expect RED until it is implemented) ──"
fail=0; pass=0
for f in tests/closures/[0-9]*.aiml; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  tier=$(grep -m1 -oE '# TIER: T[0-2]' "$f" | grep -oE 'T[0-2]' || echo "--")
  out=$(env "${env_common[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  line=$(echo "$out" | grep -E "PASS|FAIL|use-after|error:" | head -1)
  if [ $rc -eq 0 ]; then pass=$((pass+1)); printf "  ok   [%s] %-30s %s\n" "$tier" "$base" "$line"
  else fail=$((fail+1)); printf "  FAIL [%s] %-30s rc=%-3d %s\n" "$tier" "$base" "$rc" "$line"; fi
done
echo "── $pass passed, $fail failed ──"

# ── Negative probes (§9 diagnostics): each MUST be REJECTED with its `# EXPECT-ERROR:` text.
npass=0; nfail=0
for f in tests/closures/neg/*.aiml; do
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
echo "── neg: $npass rejected as expected, $nfail wrong ──"
fail=$((fail + nfail))

[ $fail -eq 0 ] && exit 0 || exit 1
