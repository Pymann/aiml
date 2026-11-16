#!/bin/bash
# Ein Fall nach spec/70: JIT (compile-only + gesamt) und AOT (Objekt, Link, Lauf),
# dazu die C-Grundlinie im SELBEN Lauf (F70-60).  Der alte run.sh kann AOT nicht
# messen -- er uebergibt `-o <case>.bin` (das ist ein OBJEKTFILE) und versucht es
# auszufuehren; mit `set -e` endet er dort.  Deshalb dieser Runner.
#
#   performance/run_case.sh fib100
set -uo pipefail

CASE="${1:?Fall fehlt, z.B. fib100}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AIML="$ROOT/out/aiml/aiml"
LIB="$ROOT/out/libaiml"
SRC="$ROOT/performance/cases/$CASE.aiml"
WORK="$ROOT/performance/results/$CASE"
BASE="$ROOT/performance/baselines/$CASE"

[ -x "$AIML" ] || { echo "kein Compiler: $AIML"; exit 1; }
[ -f "$SRC" ]  || { echo "kein Fall: $SRC"; exit 1; }
mkdir -p "$WORK"

ms() { date +%s%3N; }

echo "== Umgebung (F70-15) =="
echo "commit=$(cd "$ROOT" && git rev-parse --short HEAD) cpu=$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //') cores=$(nproc)"
echo "kernel=$(uname -r) gcc=$(gcc -dumpversion) governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unbekannt) ir_opt=none"
echo "load=$(cut -d' ' -f1-3 /proc/loadavg)"

echo; echo "== JIT: compile-only (--check), 3x =="
for i in 1 2 3; do
    t0=$(ms); "$AIML" "$SRC" --check > "$WORK/check.out" 2>&1; rc=$?; t1=$(ms)
    echo "  compile_ms=$((t1-t0)) rc=$rc"
done

echo; echo "== JIT: compile + Lauf =="
t0=$(ms); "$AIML" "$SRC" > "$WORK/jit.stdout" 2> "$WORK/jit.stderr"; rc=$?; t1=$(ms)
echo "  total_ms=$((t1-t0)) rc=$rc"
sed 's/^/  /' "$WORK/jit.stdout"

echo; echo "== AOT: Objekt, Link, Lauf (F70-4) =="
t0=$(ms); "$AIML" "$SRC" -o "$WORK/$CASE.o" > "$WORK/aot.compile.log" 2>&1; rc=$?; t1=$(ms)
echo "  obj_ms=$((t1-t0)) rc=$rc"
DEPS=""; [ -f "$WORK/$CASE.o.deps" ] && DEPS="$(tr '\n' ' ' < "$WORK/$CASE.o.deps")"
t0=$(ms)
g++ -o "$WORK/$CASE.bin" "$WORK/$CASE.o" $DEPS -L"$LIB" -laiml -lpthread -ldl -lm -lstdc++ -Wl,-rpath,"$LIB" 2> "$WORK/aot.link.log"
rc=$?; t1=$(ms)
echo "  link_ms=$((t1-t0)) rc=$rc deps=$(echo $DEPS | wc -w)"
[ $rc -ne 0 ] && sed 's/^/  /' "$WORK/aot.link.log"
if [ -x "$WORK/$CASE.bin" ]; then
    echo "  binary_bytes=$(stat -c %s "$WORK/$CASE.bin")"
    t0=$(ms); "$WORK/$CASE.bin" > "$WORK/aot.stdout" 2> "$WORK/aot.stderr"; rc=$?; t1=$(ms)
    echo "  run_total_ms=$((t1-t0)) rc=$rc"
    sed 's/^/  /' "$WORK/aot.stdout"
fi

if [ -f "$BASE/$CASE.c" ]; then
    echo; echo "== C-Grundlinie, gleicher Lauf (F70-60) =="
    t0=$(ms); gcc -O2 -o "$WORK/$CASE.c.bin" "$BASE/$CASE.c" 2> "$WORK/c.build.log"; rc=$?; t1=$(ms)
    echo "  cc_ms=$((t1-t0)) rc=$rc flags='-O2' cc=gcc-$(gcc -dumpversion)   # F70-63"
    echo "  binary_bytes=$(stat -c %s "$WORK/$CASE.c.bin")"
    t0=$(ms); "$WORK/$CASE.c.bin" > "$WORK/c.stdout" 2>&1; t1=$(ms)
    echo "  run_total_ms=$((t1-t0))"
    sed 's/^/  /' "$WORK/c.stdout"
fi
