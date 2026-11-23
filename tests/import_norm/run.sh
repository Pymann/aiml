#!/usr/bin/env bash
# run.sh — one file, two import spellings (../lib/shared.aiml and ../lib/sub/../shared.aiml).
#
# The import path must be normalised lexically, otherwise the file is compiled once but
# registered under two lib-init names: JIT calls a null pointer, AOT cannot find a symbol.
# Runs from app/, so that the resolved paths start with ".." (the case that broke before).
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
AIML="${AIML:-$ROOT/out/aiml/aiml}"
case "$AIML" in /*) ;; *) AIML="$ROOT/$AIML" ;; esac
APP="$ROOT/tests/import_norm/app"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK" "$APP/__aiml_cache"' EXIT
fail=0
cd "$APP"

out=$("$AIML" main_probe.aiml -r 2>&1); rc=$?
if [ $rc -eq 0 ] && echo "$out" | grep -q "PASS import_norm"; then echo "  ok   JIT"; else echo "  FAIL JIT (rc=$rc)"; echo "$out" | grep -v WARNING | tail -3; fail=1; fi

# libaiml.so: $LIBAIML_DIR, otherwise next to the compiler (../lib, ../libaiml), otherwise out/libaiml.
LIBDIR=""
for cand in "${LIBAIML_DIR:-}" "$(dirname "$AIML")/lib" "$(dirname "$AIML")/../lib" "$(dirname "$AIML")/../libaiml" "$ROOT/out/libaiml"; do
    [ -n "$cand" ] && [ -f "$cand/libaiml.so" ] && { LIBDIR="$(cd "$cand" && pwd)"; break; }
done
if [ -z "$LIBDIR" ]; then
    echo "  SKIP AOT (no libaiml.so found; set LIBAIML_DIR)"
elif "$AIML" main_probe.aiml -o "$WORK/probe.o" > "$WORK/build.log" 2>&1 \
   && g++ -o "$WORK/probe" "$WORK"/*.o -L"$LIBDIR" -laiml -Wl,-rpath,"$LIBDIR" > "$WORK/link.log" 2>&1; then
    out=$("$WORK/probe" 2>&1); rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "PASS import_norm"; then echo "  ok   AOT"; else echo "  FAIL AOT (rc=$rc)"; fail=1; fi
else
    echo "  FAIL AOT build/link"; grep -h -E "undefined|ERROR" "$WORK/build.log" "$WORK/link.log" | head -3; fail=1
fi
exit $fail
