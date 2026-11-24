#!/usr/bin/env bash
# Live test: sys.bundle over a socket to an AOT listener, executed there.
# Proves spec/29_2 §11/§13 — an isolated context runs WITHOUT a JIT.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f ../../exvenv/bin/activate ] && source ../../exvenv/bin/activate
LOG_DIR="tests/bundle_socket"
for n in listener sender; do
    AIML_MEM_V3=1 exodus build -c tests/bundle_socket/exodus-$n.json > "$LOG_DIR/build_$n.log" 2>&1 \
        || { echo "BUILD FAIL $n"; tail -5 "$LOG_DIR/build_$n.log"; exit 1; }
done
./bs_listener > "$LOG_DIR/listener.out" 2>&1 &
LPID=$!
sleep 1
./bs_sender  > "$LOG_DIR/sender.out" 2>&1
SRC=$?
wait $LPID; LRC=$?
echo "--- listener ---"; grep -vE "^\[INFO\]|^\[DEBUG\]" "$LOG_DIR/listener.out"
echo "--- sender ---";   grep -vE "^\[INFO\]|^\[DEBUG\]" "$LOG_DIR/sender.out"
if [ $SRC -eq 0 ] && [ $LRC -eq 0 ]; then echo "bundle_socket PASS"; exit 0; fi
echo "bundle_socket FAIL (sender=$SRC listener=$LRC)"; exit 1
