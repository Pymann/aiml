#!/usr/bin/env bash
# spec_covers_annotate.sh — writes the `# SPEC:` header line into the tests.
#
# Division of labour: the MAPPING is done by tools/spec_covers.aiml (read-only, in aiml),
# the INSERTING by this script. `sed -i '1i'` is byte-faithful, and every change
# is exactly one additional line — that can be checked completely with `git diff --stat`,
# which would not be as easy with string building in aiml.
#
# The origin is IN the key, so that commitment and guess are separable by grep:
#   `# SPEC:`   the test names the chapters itself       -> taken over
#   `# SPEC?:`  derived from the file name               -> to be confirmed
# The key is called SPEC and not COVERS, because `# COVERS:` in the labs
# (tests/closures, tests/clearance) is already taken as a PROSE field — two
# meanings for one key would be exactly the ambiguity that a
# later tool silently misreads.
# Files with neither are NOT touched and are listed: they need a
# human, and a guessed annotation would be worse than none.
#
#   bash tools/spec_covers_annotate.sh            # dry run (only counts)
#   WRITE=1 bash tools/spec_covers_annotate.sh    # writes
set -uo pipefail
cd "$(dirname "$0")/.."
REPORT="${REPORT:-logs/covers_report.tsv}"
WRITE="${WRITE:-0}"
[ -f "$REPORT" ] || { echo "report missing: $REPORT — run tools/spec_covers.aiml first"; exit 1; }

n_auth=0; n_name=0; n_none=0; n_skip=0
: > logs/covers_needs_human.txt
while IFS=$'\t' read -r fpath acol ncol dcol; do
  [ -n "${fpath:-}" ] || continue
  [ -f "$fpath" ] || continue
  auth="${acol#A=}"; byname="${ncol#N=}"
  if grep -q "^# SPEC" "$fpath"; then n_skip=$((n_skip+1)); continue; fi
  if [ -n "$auth" ]; then
    line="# SPEC: $auth"
    n_auth=$((n_auth+1))
  elif [ -n "$byname" ]; then
    line="# SPEC?: $byname   # derived from the file name, to be confirmed"
    n_name=$((n_name+1))
  else
    echo "$fpath" >> logs/covers_needs_human.txt
    n_none=$((n_none+1)); continue
  fi
  if [ "$WRITE" = "1" ]; then
    sed -i "1i $line" "$fpath" || { echo "ERROR at $fpath"; exit 1; }
  fi
done < "$REPORT"

echo "COVERS:   $n_auth  (from spec citations in the test)"
echo "COVERS?:  $n_name  (from the file name, to be confirmed)"
echo "without:  $n_none  -> logs/covers_needs_human.txt"
echo "already there: $n_skip"
[ "$WRITE" = "1" ] || echo "(dry run — WRITE=1 to write)"
