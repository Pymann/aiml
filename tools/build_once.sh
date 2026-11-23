#!/usr/bin/env bash
# One build at a time.  Overlapping `exodus build --all` take over objects
# that the other run has just produced with old content — the change is
# then NOT in the library, and you measure code that was never built.
# (Happened twice; both times it cost several rounds.)
set -uo pipefail
cd "$(dirname "$0")/.."
LOG="${1:-/tmp/aiml_build.log}"
exec 9>/tmp/.aiml_build.lock
if ! flock -n 9; then
    echo "BUILD ALREADY RUNNING — aborted" >&2
    exit 2
fi
source ../../exvenv/bin/activate
timeout 1800 exodus build --all > "$LOG" 2>&1
RC=$?
echo "exit=$RC  errors=$(grep -cE 'error:' "$LOG")"
grep -E "error:" "$LOG" | sed 's|.*/aiml/||' | sort -u | head -5
exit $RC
