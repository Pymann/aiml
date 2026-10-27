#!/bin/bash
# Binary upload round-trip: random bytes (with NULs) above the 8 MiB default
# must arrive byte-identical, and the default cap must still answer 413.
set -u
AIML_BIN="${AIML_BIN:-./out/aiml/aiml}"
DIR="$(mktemp -d)"
PORT=23196
trap 'kill $SRV 2>/dev/null; rm -rf "$DIR"' EXIT
head -c 12582912 /dev/urandom > "$DIR/in.bin"
"$AIML_BIN" tests/_http_binary_upload/server.aiml -- "$PORT" "$DIR/out.bin" >/dev/null 2>&1 &
SRV=$!
for _ in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$PORT/" && break; sleep 0.2; done
REPLY=$(curl -s -X PUT --data-binary @"$DIR/in.bin" "http://127.0.0.1:$PORT/upload")
WANT="$(stat -c %s "$DIR/in.bin") $(sha256sum "$DIR/in.bin" | cut -d' ' -f1)"
FAIL=0
[ "$REPLY" = "$WANT" ] || { echo "[FAIL] reply '$REPLY' != '$WANT'"; FAIL=1; }
cmp -s "$DIR/in.bin" "$DIR/out.bin" || { echo "[FAIL] stored file differs"; FAIL=1; }
[ $FAIL -eq 0 ] && echo "PASS: 12 MiB binary upload round-trip"
exit $FAIL
