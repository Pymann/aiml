#!/usr/bin/env bash
# run.sh — eine Datei, zwei Importschreibweisen (../lib/shared.aiml und ../lib/sub/../shared.aiml).
#
# Der Importpfad muss lexikalisch normalisiert werden, sonst wird die Datei einmal übersetzt, aber
# unter zwei Lib-Init-Namen registriert: JIT ruft einen Nullzeiger, AOT findet ein Symbol nicht.
# Läuft aus app/, damit die aufgelösten Pfade mit ".." beginnen (der Fall, der vorher brach).
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

# libaiml.so: $LIBAIML_DIR, sonst neben dem Compiler (../lib, ../libaiml), sonst out/libaiml.
LIBDIR=""
for cand in "${LIBAIML_DIR:-}" "$(dirname "$AIML")/lib" "$(dirname "$AIML")/../lib" "$(dirname "$AIML")/../libaiml" "$ROOT/out/libaiml"; do
    [ -n "$cand" ] && [ -f "$cand/libaiml.so" ] && { LIBDIR="$(cd "$cand" && pwd)"; break; }
done
if [ -z "$LIBDIR" ]; then
    echo "  SKIP AOT (keine libaiml.so gefunden; LIBAIML_DIR setzen)"
elif "$AIML" main_probe.aiml -o "$WORK/probe.o" > "$WORK/build.log" 2>&1 \
   && g++ -o "$WORK/probe" "$WORK"/*.o -L"$LIBDIR" -laiml -Wl,-rpath,"$LIBDIR" > "$WORK/link.log" 2>&1; then
    out=$("$WORK/probe" 2>&1); rc=$?
    if [ $rc -eq 0 ] && echo "$out" | grep -q "PASS import_norm"; then echo "  ok   AOT"; else echo "  FAIL AOT (rc=$rc)"; fail=1; fi
else
    echo "  FAIL AOT build/link"; grep -h -E "undefined|ERROR" "$WORK/build.log" "$WORK/link.log" | head -3; fail=1
fi
exit $fail
