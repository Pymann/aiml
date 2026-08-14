#!/bin/bash
# Safe wrapper for running the AIML compiler with strict resource limits.
# Used while debugging compiler crashes that would otherwise freeze the machine.
#
# Limits:
#   -v   4 GB virtual memory          (prevents OOM thrash)
#   -s 65 MB  stack                   (catches deep-recursion / stack-overflow)
#   -t  20 s CPU time                 (catches infinite loops)
#   -d   2 GB data segment            (heap)
#
# Usage:
#   ./run_safe.sh tests/test_foo.aiml [extra-args]
#   AIML_TRACE='*' ./run_safe.sh tests/test_foo.aiml

set -u
ulimit -v 4000000  || true   # 4 GB virtual mem (kB)
ulimit -s 65536    || true   # 64 MB stack    (kB)
ulimit -t 20       || true   # 20 s CPU
ulimit -d 2000000  || true   # 2 GB data      (kB)
ulimit -c 0        || true   # no core dumps  (avoids huge disk writes on crash)

AIML_BIN="./out/aiml/aiml"
if [ ! -x "$AIML_BIN" ]; then
    echo "Compiler binary not found at $AIML_BIN" >&2
    exit 127
fi

echo "[run_safe] limits: vm=4G stack=64M cpu=20s data=2G core=off"
echo "[run_safe] cmd: $AIML_BIN $*"
exec "$AIML_BIN" "$@"
