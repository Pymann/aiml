#!/usr/bin/env bash
# The globals lint (spec/21 §6.2 rule 5): WHAT warns, what does not, and that it
# can be switched off.  As a driver, because a warning does not change the return
# value — via `# Expected exit code` it could not be checked.
set -uo pipefail
cd "$(dirname "$0")/../.."
AIML=./out/aiml/aiml
PROBE=tests/lint_globals/probe.aiml
LOG=tests/lint_globals/out.log
fails=0

check() { # name, bedingung
    if [ "$2" = "1" ]; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails+1)); fi
}

AIML_LINT_GLOBALS=1 $AIML "$PROBE" > "$LOG" 2>&1
warn() { grep -c "module-level global '$1'" "$LOG"; }

check "const i32 does NOT warn"     "$([ "$(warn MAX_TRIES)" = "0" ] && echo 1)"
check "const string does NOT warn"     "$([ "$(warn GREETING)"  = "0" ] && echo 1)"
check "writable i32 warns"          "$([ "$(warn g_counter)" -ge "1" ] && echo 1)"
check "owning ptr<> warns"          "$([ "$(warn g_box)"     -ge "1" ] && echo 1)"
check "ptr<> warning names the reason" \
    "$(grep -q "g_box.*OWNS its value" "$LOG" && echo 1)"

AIML_LINT_GLOBALS=0 $AIML "$PROBE" > "$LOG.off" 2>&1
check "AIML_LINT_GLOBALS=0 stays silent" \
    "$([ "$(grep -c 'module-level global' "$LOG.off")" = "0" ] && echo 1)"

[ "$fails" = "0" ] && { echo "lint_globals PASS"; exit 0; }
echo "lint_globals FAIL ($fails)"; exit 1
