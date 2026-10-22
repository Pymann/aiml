#!/usr/bin/env bash
# Der Globals-Lint (spec/21 §6.2 Regel 5): WAS warnt, was nicht, und dass er
# sich abschalten laesst.  Als Treiber, weil eine Warnung den Rueckgabewert
# nicht aendert — ueber `# Expected exit code` waere sie nicht pruefbar.
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

check "const i32 warnt NICHT"        "$([ "$(warn MAX_TRIES)" = "0" ] && echo 1)"
check "const string warnt NICHT"     "$([ "$(warn GREETING)"  = "0" ] && echo 1)"
check "schreibbares i32 warnt"       "$([ "$(warn g_counter)" -ge "1" ] && echo 1)"
check "besitzendes ptr<> warnt"      "$([ "$(warn g_box)"     -ge "1" ] && echo 1)"
check "ptr<>-Warnung nennt den Grund" \
    "$(grep -q "g_box.*OWNS its value" "$LOG" && echo 1)"

AIML_LINT_GLOBALS=0 $AIML "$PROBE" > "$LOG.off" 2>&1
check "AIML_LINT_GLOBALS=0 schweigt" \
    "$([ "$(grep -c 'module-level global' "$LOG.off")" = "0" ] && echo 1)"

[ "$fails" = "0" ] && { echo "lint_globals PASS"; exit 0; }
echo "lint_globals FAIL ($fails)"; exit 1
