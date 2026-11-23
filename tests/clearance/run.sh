#!/usr/bin/env bash
# run.sh — probes for spec/65 (symbol clearance).
#
#   bash tests/clearance/run.sh              # evaluates STAGE=step2 (default)
#   STAGE=step3 bash tests/clearance/run.sh  # + opt-in flow check
#   STAGE=session ...                                  # + restricted session
#   STAGE=host ...                                     # + probes that need a host
#   STAGE=write ...                                    # + write side (create/replace/collision)
#   ONLY=07 ...                                        # only matching probes
#
# Every probe states in its header which implementation step it needs:
#   # NEEDS: step2   -> accept the syntax + validate `level`/`grants` (always on)
#   # NEEDS: step3   -> opt-in flow check (AIML_STATIC_CLEARANCE)
#   # NEEDS: session -> restricted session, checkable on its own (AIML_CLEARANCE)
#   # NEEDS: exports -> needs the host's export table (AIML_EXPORTS), but no
#                      callable addresses — the expectation is a diagnostic
#   # NEEDS: host    -> needs a HOST with CALLABLE symbols (signature + address),
#                      and `level:` in one's own source is self-certification (F65-27)
#   # NEEDS: write   -> the WRITE SIDE (§5b): `create_level`/`replace_level`, the
#                      collision error and the hot-reload permission (F65-26 … F65-32)
#   # NEEDS: writedown -> the write-down rule (F65-34), value flow INSIDE the body
#   # NEEDS: dynload -> needs spec/29_2 §4.3 (dynamic loading).  Not this
#                      chapter: `sys.bundle` can only bundle a function the host
#                      compiled ITSELF — a NEW name cannot get into the bundle that way,
#                      and the `clearance-create` branch stays unreachable.
# Probes above STAGE are reported as PENDING and do NOT count
# as a failure — otherwise the lab would be permanently red and thus worthless.
#
# Session per probe (set by the host, never from the source — spec/65 F65-6):
#   # CLEARANCE: none|<1..255>   # CREATE-LEVEL: N   # REPLACE-LEVEL: N
#   # STATIC-CLEARANCE: on
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML="${AIML:-out/aiml/aiml}"
ONLY="${ONLY:-}"
STAGE="${STAGE:-step2}"

rank() { case "$1" in step2) echo 1;; step3) echo 2;; session) echo 3;; guard) echo 4;; exports) echo 5;; host) echo 6;; write) echo 7;; writedown) echo 8;; dynload) echo 9;; *) echo 1;; esac; }
WANT=$(rank "$STAGE")

session_env() {
  local f="$1" cl st cr rp ex
  cl=$(grep -m1 -oE '^# CLEARANCE: *[a-z0-9]+'  "$f" | awk '{print $3}')
  cr=$(grep -m1 -oE '^# CREATE-LEVEL: *[0-9]+'  "$f" | awk '{print $3}')
  rp=$(grep -m1 -oE '^# REPLACE-LEVEL: *[0-9]+' "$f" | awk '{print $3}')
  ex=$(grep -m1 -oE '^# EXPORTS: *[^ ]+' "$f" | awk '{print $3}')
  # F65-24 second route: the ADDRESS comes from a preloaded library, not from
  # `sys.export_symbol`.  That is the route of a host that lets foreign source be
  # compiled — it only learns the script at runtime (cosmos doc/spec/108 F108-9).
  pl=$(grep -m1 -oE '^# PRELOAD: *[^ ]+' "$f" | awk '{print $3}')
  st=$(grep -m1 -oE '^# STATIC-CLEARANCE: *on'  "$f")
  [ -n "${cl:-}" ] && [ "$cl" != "none" ] && echo "AIML_CLEARANCE=$cl"
  [ -n "${cr:-}" ] && echo "AIML_CREATE_LEVEL=$cr"
  [ -n "${rp:-}" ] && echo "AIML_REPLACE_LEVEL=$rp"
  [ -n "${ex:-}" ] && echo "AIML_EXPORTS=$ex"
  [ -n "${pl:-}" ] && echo "LD_PRELOAD=$pl"
  [ -n "${st:-}" ] && echo "AIML_STATIC_CLEARANCE=1"
}
needs_of() { grep -m1 -oE '^# NEEDS: *[a-z0-9]+' "$1" | awk '{print $3}'; }

# The host library for the PRELOAD probes: build once, not per probe.  The system's
# gcc is fine for this (user decision 2026-10-16).
HOSTLIB=tests/clearance/fixtures/libclearance_host.so
HOSTSRC=tests/clearance/fixtures/host_lib.c
if [ -f "$HOSTSRC" ] && { [ ! -f "$HOSTLIB" ] || [ "$HOSTSRC" -nt "$HOSTLIB" ]; }; then
  gcc -shared -fPIC -o "$HOSTLIB" "$HOSTSRC" || echo "  !! host library cannot be built"
fi

pass=0; fail=0; pend=0
echo "── spec/65 Clearance-Labor (STAGE=$STAGE) ──"
for f in tests/clearance/[0-9]*.aiml; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  need=$(needs_of "$f"); [ -z "$need" ] && need=step2
  mapfile -t sess < <(session_env "$f")
  if [ "$(rank "$need")" -gt "$WANT" ]; then
    pend=$((pend+1)); printf "  ..   %-42s PENDING (needs %s)\n" "$base" "$need"; continue
  fi
  out=$(env "${sess[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  line=$(echo "$out" | grep -E "PASS|FAIL|error:" | head -1)
  if [ $rc -eq 0 ]; then pass=$((pass+1)); printf "  ok   %-42s %s\n" "$base" "$line"
  else fail=$((fail+1));  printf "  FAIL %-42s rc=%-3d %s\n" "$base" "$rc" "$line"; fi
done

npass=0; nfail=0
for f in tests/clearance/neg/*.aiml; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  [ -n "$ONLY" ] && [[ "$base" != *"$ONLY"* ]] && continue
  need=$(needs_of "$f"); [ -z "$need" ] && need=step2
  want=$(grep -m1 -oE '# EXPECT-ERROR: .*' "$f" | sed 's/# EXPECT-ERROR: //')
  if [ "$(rank "$need")" -gt "$WANT" ]; then
    pend=$((pend+1)); printf "  ..   %-42s PENDING (needs %s)\n" "$base" "$need"; continue
  fi
  mapfile -t sess < <(session_env "$f")
  out=$(env "${sess[@]}" "$AIML" "$f" -r 2>&1); rc=$?
  if [ $rc -eq 0 ]; then
    nfail=$((nfail+1)); printf "  FAIL %-42s ACCEPTED, expected: %s\n" "$base" "$want"
  elif echo "$out" | grep -qF "$want"; then
    npass=$((npass+1)); printf "  ok   %-42s rejected: %s\n" "$base" "$want"
  else
    nfail=$((nfail+1))
    printf "  FAIL %-42s wrong reason (expected %s): %s\n" "$base" "$want" \
           "$(echo "$out" | grep -E 'error:' | head -1)"
  fi
done

echo "── positive $pass ok / $fail fail · negative $npass ok / $nfail fail · pending $pend ──"
[ $fail -eq 0 ] && [ $nfail -eq 0 ]
