#!/usr/bin/env bash
# Live-Test: sys.bundle ueber Socket an einen AOT-Listener, dort ausgefuehrt.
# Beweist spec/29_2 §11/§13 — isolierter Kontext laeuft OHNE JIT.
set -uo pipefail
cd "$(dirname "$0")/../.."
source ../../exvenv/bin/activate
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
