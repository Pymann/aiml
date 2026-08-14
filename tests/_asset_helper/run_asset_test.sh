#!/bin/bash
# Test-Helper for Asset-Registry tests.  Used by run_all_tests.sh when the
# test file has a `# AssetRoots: <comma-list>` header.  Invokes the aiml
# compiler with the parsed --asset-roots flag and the test's own file.
#
# Usage: tests/_asset_helper/run_asset_test.sh <test.aiml>
# Exit code: forwards aiml's exit code (0 = test PASS for positive tests,
# 1 = test PASS for negative tests where the test file specifies
# `# Expected exit code: 1` — that comparison happens in run_all_tests.sh).

set -eo pipefail

TEST="${1:?usage: $0 <test.aiml>}"
if [[ ! -f "$TEST" ]]; then
    echo "ERROR: test file not found: $TEST" >&2
    exit 2
fi

# Parse `# AssetRoots: a,b,c` from the test file header.  Falls back to
# empty if missing (the test then runs without any --asset-roots flag,
# which is itself a valid scenario).
ROOTS=$(grep -m1 '^# AssetRoots:' "$TEST" | sed 's/^# AssetRoots:[[:space:]]*//' | tr -d '[:space:]' || true)

AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
if [[ ! -x "$AIML_BIN" ]]; then
    echo "ERROR: aiml binary not found at $AIML_BIN" >&2
    exit 2
fi

if [[ -n "$ROOTS" ]]; then
    "$AIML_BIN" --asset-roots="$ROOTS" "$TEST"
else
    "$AIML_BIN" "$TEST"
fi
