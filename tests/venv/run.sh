#!/usr/bin/env bash
# run.sh — aiml environments (spec/68): `aiml venv <dir>`, the layout, `env`, the RUNPATHs, the
# venv's package table, and the venv as something that is moved around (a layer).
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
AIML="${AIML:-$ROOT/out/aiml/aiml}"
case "$AIML" in /*) ;; *) AIML="$ROOT/$AIML" ;; esac
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
runpath() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(RUNPATH).*\[\(.*\)\]/\1/p'; }

VENV="$WORK/v"
echo "── aiml venv (spec/68) ──"
out=$("$AIML" venv "$VENV" 2>&1); rc=$?
check "creates the venv from the build tree (F68-10/11)" '[ $rc -eq 0 ]'
[ $rc -eq 0 ] || echo "$out" | head -5
check "layout: bin/aiml, lib/libaiml.so, lib/aiml/aiml.packages.json, env (F68-1)" \
  '[ -x "$VENV/bin/aiml" ] && [ -f "$VENV/lib/libaiml.so" ] && [ -f "$VENV/lib/aiml/aiml.packages.json" ] && [ -f "$VENV/env" ]'
check "the copies are the build's compiler and runtime" \
  'cmp -s "$AIML" "$VENV/bin/aiml" && cmp -s "$(dirname "$AIML")/../libaiml/libaiml.so" "$VENV/lib/libaiml.so"'
check "no temp files left behind (F68-12)" '[ -z "$(find "$VENV" -name "*.tmp.*")" ]'
check "bin/aiml: RUNPATH starts with \$ORIGIN/../lib (F68-1)" '[[ "$(runpath "$VENV/bin/aiml")" == "\$ORIGIN/../lib"* ]]'
check "lib/libaiml.so: RUNPATH starts with \$ORIGIN (F68-1)" '[[ "$(runpath "$VENV/lib/libaiml.so")" == "\$ORIGIN"* ]]'
check "empty package table" 'grep -q "\"packages\"" "$VENV/lib/aiml/aiml.packages.json"'
check "no cache inside the venv (F68-2)" '[ -z "$(find "$VENV" -name "__aiml_cache")" ]'

echo "── env (F68-20/21) ──"
check "first line is the venv marker" 'head -1 "$VENV/env" | grep -q "^# aiml venv"'
envout=$(env -i PATH=/usr/bin:/bin sh -c ". '$VENV/env'; echo \"\$AIML_VENV|\$AIML|\$(command -v aiml)|\$PATH\"")
check "sets AIML_VENV, AIML and PATH; aiml on PATH is the venv's" \
  '[ "$envout" = "$VENV|$VENV/bin/aiml|$VENV/bin/aiml|$VENV/bin:/usr/bin:/bin" ]'
check "sets nothing else (no LIBRARY_PATH / LD_LIBRARY_PATH)" \
  '[ "$(grep -c "^export" "$VENV/env")" = 3 ] && ! grep -q "LIBRARY_PATH" "$VENV/env"'
envempty=$(env -i PATH= /bin/sh -c ". '$VENV/env'; echo \"\$PATH\"")
check "PATH without a trailing ':' when it was empty" '[ "$envempty" = "$VENV/bin" ]'

# A project that uses a package; the table lookup must find none of its own (it lives in /tmp).
PROJ="$WORK/proj"; mkdir -p "$PROJ"
cp tests/packages/main_file.aiml "$PROJ/"
mkdir -p "$VENV/lib/aiml/demo"; cp -r tests/packages/demo_pkg/. "$VENV/lib/aiml/demo/"
printf '{"packages": {"demo": {"path": "demo", "version": "1.0.0"}}}\n' > "$VENV/lib/aiml/aiml.packages.json"
inproj() { (cd "$PROJ" && env -i HOME="$WORK" AIML_MEM_V3=1 AIML_CACHE="$WORK/cache" sh -c ". '$VENV/env'; $1" 2>&1); }

echo "── packages of the venv (F68-30/31/32) ──"
out=$(inproj "aiml main_file.aiml"); rc=$?
check "JIT run via PATH imports @demo from the venv's table" '[ $rc -eq 0 ] && echo "$out" | grep -q "PASS package import from a file"'
[ $rc -eq 0 ] || echo "$out" | grep -E "ERROR|FAIL" | head -3
mkdir -p "$PROJ/mine/demo"; cp -r tests/packages/demo_pkg/. "$PROJ/mine/demo/"
sed -i 's/demo greets/project greets/' "$PROJ/mine/demo/lib/greet.aiml"
printf '{"packages": {"demo": {"path": "mine/demo", "version": "2.0.0"}}}\n' > "$PROJ/aiml.packages.json"
out=$(inproj "aiml main_file.aiml"); rc=$?
check "the project's own table wins over the venv's" '[ $rc -eq 0 ] && echo "$out" | grep -q "project greets"'
rm -f "$PROJ/aiml.packages.json"
out=$(cd "$PROJ" && env AIML_MEM_V3=1 AIML_CACHE="$WORK/cache" "$AIML" main_file.aiml 2>&1); rc=$?
check "the build-tree compiler (no venv) does not see it: error names the fix" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus deps install"'
out=$(cd "$PROJ" && env AIML_MEM_V3=1 AIML_CACHE="$WORK/cache" AIML_VENV="$VENV" "$AIML" main_file.aiml 2>&1); rc=$?
check "AIML_VENV does not make a foreign compiler read the venv (F68-31)" '[ $rc -ne 0 ]'

echo "── AOT program against the venv (F68-22) ──"
mkdir -p "$WORK/obj"
out=$(inproj "aiml main_file.aiml -o '$WORK/obj/main.o'"); rc=$?
deps=$(ls "$WORK"/obj/*.o 2>/dev/null | grep -v '/main.o$' | tr '\n' ' ')
if [ $rc -eq 0 ] && g++ -o "$VENV/bin/demo_prog" "$WORK/obj/main.o" $deps -L"$VENV/lib" -laiml \
     '-Wl,-rpath,$ORIGIN/../lib' -lpthread -ldl 2> "$WORK/link.err"; then
  out=$(env -i "$VENV/bin/demo_prog" 2>&1); rc=$?
  check "bin/demo_prog runs without LD_LIBRARY_PATH, from \$ORIGIN/../lib" '[ $rc -eq 0 ] && echo "$out" | grep -q "PASS"'
  check "it loads the venv's libaiml.so" \
    'env -i LD_TRACE_LOADED_OBJECTS=1 "$VENV/bin/demo_prog" | grep -q "libaiml.so => $VENV/bin/../lib/libaiml.so"'
else
  bad "compile/link demo_prog (rc=$rc)"; echo "$out" | head -3; head -3 "$WORK/link.err"
fi

echo "── refusing and updating (F68-13/14) ──"
FOREIGN="$WORK/foreign"; mkdir -p "$FOREIGN"; echo keep > "$FOREIGN/file"
out=$("$AIML" venv "$FOREIGN" 2>&1); rc=$?
check "a non-empty directory that is no venv is refused, untouched" \
  '[ $rc -ne 0 ] && [ "$(ls "$FOREIGN")" = file ] && echo "$out" | grep -qi "venv"'
mkdir -p "$WORK/it's"
out=$("$AIML" venv "$WORK/it's/v" 2>&1); rc=$?
check "a path with a single quote is refused (F68-20)" '[ $rc -ne 0 ] && [ ! -e "$WORK/it'"'"'s/v/env" ]'
out=$("$AIML" venv "$WORK/nort" --runtime "$WORK/missing.so" 2>&1); rc=$?
check "a missing runtime is refused and named" '[ $rc -ne 0 ] && echo "$out" | grep -q -- "--runtime"'
out=$("$VENV/bin/aiml" venv "$VENV" 2>&1); rc=$?
check "update from inside the venv keeps packages and table" \
  '[ $rc -eq 0 ] && [ -d "$VENV/lib/aiml/demo" ] && grep -q "\"demo\"" "$VENV/lib/aiml/aiml.packages.json" && cmp -s "$AIML" "$VENV/bin/aiml"'
check "a venv-made venv takes its runtime from lib/ (F68-11)" \
  '"$VENV/bin/aiml" venv "$WORK/v3" >/dev/null 2>&1 && cmp -s "$VENV/lib/libaiml.so" "$WORK/v3/lib/libaiml.so"'
mv "$VENV" "$WORK/moved"; MOVED="$WORK/moved"
out=$("$MOVED/bin/aiml" venv "$MOVED" 2>&1); rc=$?
check "a moved venv rewrites env for its new place" \
  '[ $rc -eq 0 ] && grep -q "AIML_VENV='"'"'$MOVED'"'"'" "$MOVED/env" && ! grep -q "$WORK/v'"'"'" "$MOVED/env"'
out=$(cd "$PROJ" && env -i HOME="$WORK" AIML_MEM_V3=1 AIML_CACHE="$WORK/cache" sh -c ". '$MOVED/env'; aiml main_file.aiml" 2>&1); rc=$?
check "the moved venv still finds its packages (F68-32)" '[ $rc -eq 0 ] && echo "$out" | grep -q PASS'
out=$(env -i "$MOVED/bin/demo_prog" 2>&1); rc=$?
check "and its AOT program still runs (relocatable, F68-1)" '[ $rc -eq 0 ]'

[ $fail -eq 0 ] && echo "venv: PASS" || echo "venv: FAIL"
exit $fail
