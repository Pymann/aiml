#!/bin/bash
# A multi-file program started with program arguments (`-- ...`) must be able
# to call into its imports (the argv JIT path used to skip loading them).
AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
OUT=$("$AIML_BIN" tests/_jit_args/main.aiml -- world 2>&1 | grep -v WARNING)
if echo "$OUT" | grep -q "^hello world$"; then
    echo "PASS jit run with args + imports"; exit 0
fi
echo "[FAIL] output: $OUT"; exit 1
