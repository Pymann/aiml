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
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus pkg install"'
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
check "no lib/wasm32 without one next to the runtime (F68-94)" '[ ! -e "$WORK/v3/lib/wasm32" ]'
REL="$WORK/release"; mkdir -p "$REL/bin" "$REL/lib/wasm32"
cp "$VENV/bin/aiml" "$REL/bin/aiml"; cp "$VENV/lib/libaiml.so" "$REL/lib/libaiml.so"
printf 'archive' > "$REL/lib/wasm32/libaiml_wasm.a"; printf '3.1.74\n' > "$REL/lib/wasm32/emsdk.version"
out=$("$REL/bin/aiml" venv "$WORK/vw" 2>&1); rc=$?
check "lib/wasm32 next to the runtime comes along into the venv (F68-94)" \
  '[ $rc -eq 0 ] && cmp -s "$REL/lib/wasm32/libaiml_wasm.a" "$WORK/vw/lib/wasm32/libaiml_wasm.a" && cmp -s "$REL/lib/wasm32/emsdk.version" "$WORK/vw/lib/wasm32/emsdk.version"'
check "and on to a venv made from that venv (F68-94)" \
  '"$WORK/vw/bin/aiml" venv "$WORK/vw2" >/dev/null 2>&1 && [ -f "$WORK/vw2/lib/wasm32/libaiml_wasm.a" ]'
mv "$VENV" "$WORK/moved"; MOVED="$WORK/moved"
out=$("$MOVED/bin/aiml" venv "$MOVED" 2>&1); rc=$?
check "a moved venv rewrites env for its new place" \
  '[ $rc -eq 0 ] && grep -q "AIML_VENV='"'"'$MOVED'"'"'" "$MOVED/env" && ! grep -q "$WORK/v'"'"'" "$MOVED/env"'
out=$(cd "$PROJ" && env -i HOME="$WORK" AIML_MEM_V3=1 AIML_CACHE="$WORK/cache" sh -c ". '$MOVED/env'; aiml main_file.aiml" 2>&1); rc=$?
check "the moved venv still finds its packages (F68-32)" '[ $rc -eq 0 ] && echo "$out" | grep -q PASS'
out=$(env -i "$MOVED/bin/demo_prog" 2>&1); rc=$?
check "and its AOT program still runs (relocatable, F68-1)" '[ $rc -eq 0 ]'

echo "── installing tools (§6) ──"
# The cache the install must use (F68-52) — and must not put in the venv.
export AIML_CACHE="$WORK/cache"
# F68-53/§7: an install builds with the venv's own `aiml build`.  exodus and Python on PATH
# are traps that fail loudly, so a build that still reached for them would show.
NOPY="$WORK/nopy"; mkdir -p "$NOPY"
for t in exodus python3 python; do
    printf '#!/bin/sh\necho "%s must not run" >&2; exit 99\n' "$t" > "$NOPY/$t"; chmod +x "$NOPY/$t"
done
export PATH="$NOPY:$PATH"
# A tool: a program plus an importable package, built with the venv's own compiler.
TOOL="$WORK/tool"; mkdir -p "$TOOL/lib"
cat > "$TOOL/hallo.aiml" <<'EOT'
- def:
    name: main
    return: i32
    body:
      - print: ["werkzeug laeuft"]
      - return: 0
EOT
cat > "$TOOL/lib/greet.aiml" <<'EOT'
- def:
    name: greet
    return: i32
    body:
      - print: ["werkzeug-paket"]
      - return: 0
EOT
cat > "$TOOL/exodus.json" <<'EOT'
{
    "$schema": "exodus.project.config-1.0",
    "name": "werkzeug", "version": "0.3.0",
    "output_type": "executable",
    "sources": ["hallo.aiml"],
    "compiler": { "name": "$AIML", "lang_standard": "17", "flags": [] },
    "linker": { "name": "g++", "flags": ["-Wl,-rpath,$ORIGIN/../lib"],
                "libraries": ["aiml", "pthread", "dl"],
                "library_paths": ["$AIML_VENV/lib"] },
    "artifact_in_cwd": true, "optimization": "0", "debug": false,
    "env": { "AIML_MEM_V3": "1" }
}
EOT
cat > "$TOOL/aiml.tool.json" <<'EOT'
{ "name": "werkzeug", "version": "0.3.0",
  "programs": ["werkzeug"], "package": "lib" }
EOT
# The program the build is expected to produce (artifact_in_cwd + name).
V2="$WORK/v2"
"$AIML" venv "$V2" > /dev/null 2>&1
out=$("$AIML" venv "$V2" install werkzeug --path "$TOOL" 2>&1); rc=$?
check "install --path builds and places the program (F68-50/53/54)" \
  '[ $rc -eq 0 ] && [ -x "$V2/bin/werkzeug" ]'
check "built by the venv's aiml build: neither exodus nor Python ran (F68-53, §7)" \
  '! echo "$out" | grep -q "must not run"'
[ $rc -eq 0 ] || echo "$out" | tail -4
check "the program runs out of the venv" \
  'out2=$(env -i "$V2/bin/werkzeug" 2>&1); echo "$out2" | grep -q "werkzeug laeuft"'
check "the package landed in lib/aiml and in the table (F68-54)" \
  '[ -f "$V2/lib/aiml/werkzeug/greet.aiml" ] && grep -q "\"werkzeug\"" "$V2/lib/aiml/aiml.packages.json"'
check "the record names source, commit and files (F68-56)" \
  '[ -f "$V2/lib/aiml/aiml.installed.json" ] && grep -q "path:" "$V2/lib/aiml/aiml.installed.json"'
check "list prints it and marks a path install unreproducible" \
  'lout=$("$AIML" venv "$V2" list 2>&1); echo "$lout" | grep -q werkzeug && echo "$lout" | grep -qi "unreprod"'
check "nothing was fetched or built inside the venv (F68-52)" \
  '[ ! -e "$V2/lib/aiml/werkzeug/exodus.json" ] && [ -z "$(find "$V2" -name "*.o")" ]'
# package ".": the repository root, so that @name/<dir>/x.aiml means what it says — but
# without what the build left there (F68-54).
V3="$WORK/v3b"; "$AIML" venv "$V3" > /dev/null 2>&1
printf '{ "name": "werkzeug", "version": "0.3.0", "programs": ["werkzeug"], "package": "." }\n' \
  > "$TOOL/aiml.tool.json"
out=$("$AIML" venv "$V3" install werkzeug --path "$TOOL" 2>&1); rc=$?
check "package '.' puts the tree in, @werkzeug/lib/greet.aiml resolves" \
  '[ $rc -eq 0 ] && [ -f "$V3/lib/aiml/werkzeug/lib/greet.aiml" ]'
check "and the build leftovers stay out (out/, __aiml_cache, *.o)" \
  '[ ! -e "$V3/lib/aiml/werkzeug/out" ] && [ ! -e "$V3/lib/aiml/werkzeug/__aiml_cache" ] && [ -z "$(find "$V3/lib/aiml" -name "*.o")" ]'
check "and the program is not shipped twice (bin/ only)" \
  '[ -x "$V3/bin/werkzeug" ] && [ ! -e "$V3/lib/aiml/werkzeug/werkzeug" ]'
printf '{ "name": "werkzeug", "version": "0.3.0", "programs": ["werkzeug"], "package": "lib" }\n' \
  > "$TOOL/aiml.tool.json"
# A working tree is no version: --path rebuilds every time (F68-50/57).
out=$("$AIML" venv "$V2" install werkzeug --path "$TOOL" 2>&1); rc=$?
check "a second --path install rebuilds and stays green" \
  '[ $rc -eq 0 ] && [ -x "$V2/bin/werkzeug" ]'
out=$("$AIML" venv "$V2" remove werkzeug 2>&1); rc=$?
check "remove deletes exactly what was placed (F68-56)" \
  '[ $rc -eq 0 ] && [ ! -e "$V2/bin/werkzeug" ] && [ ! -d "$V2/lib/aiml/werkzeug" ] && ! grep -q "\"werkzeug\"" "$V2/lib/aiml/aiml.packages.json"'
check "and leaves the venv itself alone" '[ -x "$V2/bin/aiml" ] && [ -f "$V2/env" ]'

echo "── configs: which configurations make the tool (F68-53) ──"
MANY="$WORK/many"; mkdir -p "$MANY"; cp "$TOOL/hallo.aiml" "$MANY/"
cp "$TOOL/exodus.json" "$MANY/exodus-gut.json"
# A source that does not compile.  A source that is MISSING would not do: exodus
# reports errorlevel 0 for a configuration whose sources resolve to nothing and
# writes no artifact (reported 2026-10-31).
printf -- '- def:\n    name: main\n    return: i32\n    body:\n      - this is not aiml\n' > "$MANY/kaputt.aiml"
sed 's|"sources": \["hallo.aiml"\]|"sources": ["kaputt.aiml"]|; s|"name": "werkzeug"|"name": "kaputt"|' \
  "$TOOL/exodus.json" > "$MANY/exodus-kaputt.json"
V5="$WORK/v5"; "$AIML" venv "$V5" > /dev/null 2>&1
printf '{ "name": "werkzeug", "version": "0.3.0", "programs": ["werkzeug"] }\n' > "$MANY/aiml.tool.json"
out=$("$AIML" venv "$V5" install werkzeug --path "$MANY" 2>&1); rc=$?
check "without configs a broken configuration fails the install (--all)" '[ $rc -ne 0 ]'
printf '{ "name": "werkzeug", "version": "0.3.0", "configs": ["exodus-gut.json"], "programs": ["werkzeug"] }\n' \
  > "$MANY/aiml.tool.json"
out=$("$AIML" venv "$V5" install werkzeug --path "$MANY" 2>&1); rc=$?
check "configs builds exactly those; the broken one is left alone (F68-53)" \
  '[ $rc -eq 0 ] && [ -x "$V5/bin/werkzeug" ]'
check "the named configuration shows up in the build output" 'echo "$out" | grep -q "exodus-gut.json"'
printf '{ "name": "werkzeug", "version": "0.3.0", "configs": ["exodus-weg.json"], "programs": ["werkzeug"] }\n' \
  > "$MANY/aiml.tool.json"
out=$("$AIML" venv "$V5" install werkzeug --path "$MANY" 2>&1); rc=$?
check "a configs entry that is not there is refused, named (F68-53)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus-weg.json"'

echo "── refusing (F68-54/53/51/6.1) ──"
NOMAN="$WORK/noman"; mkdir -p "$NOMAN"; cp "$TOOL/exodus.json" "$TOOL/hallo.aiml" "$NOMAN/"
out=$("$AIML" venv "$V2" install nomanifest --path "$NOMAN" 2>&1); rc=$?
check "a tree without aiml.tool.json is refused, named (F68-54)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "aiml.tool.json"'
WRONGNAME="$WORK/wrongname"; mkdir -p "$WRONGNAME"
cp "$TOOL/exodus.json" "$TOOL/hallo.aiml" "$WRONGNAME/"
printf '{ "name": "anders", "programs": ["werkzeug"] }\n' > "$WRONGNAME/aiml.tool.json"
out=$("$AIML" venv "$V2" install werkzeug --path "$WRONGNAME" 2>&1); rc=$?
check "a manifest whose name disagrees is refused (F68-54)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "anders"'
HARDWIRED="$WORK/hardwired"; mkdir -p "$HARDWIRED"
cp "$TOOL/hallo.aiml" "$TOOL/aiml.tool.json" "$HARDWIRED/"
sed 's|"\$AIML"|"../aiml/out/aiml/aiml"|' "$TOOL/exodus.json" > "$HARDWIRED/exodus.json"
out=$("$AIML" venv "$V2" install werkzeug --path "$HARDWIRED" 2>&1); rc=$?
check "a config naming a build tree is refused with F68-23 (F68-53)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "F68-23"'
out=$("$AIML" venv "$V2" install kon --kontor http://127.0.0.1:1 2>&1); rc=$?
check "a kontor that gives no answer is refused, named (F68-62)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "127.0.0.1:1"'
out=$("$AIML" venv "$V2" install --all 2>&1); rc=$?
check "install --all without a source is refused (F68-66)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q -- "--kontor"'
out=$("$AIML" venv "$V2" remove gibtsnicht 2>&1); rc=$?
check "removing something that is not installed is refused" '[ $rc -ne 0 ]'

echo "── install from a git repository (F68-50/51/52) ──"
if command -v git > /dev/null 2>&1; then
  REPO="$WORK/repo.git"
  git init -q --bare "$REPO"
  SRC="$WORK/src"; cp -r "$TOOL" "$SRC"; rm -rf "$SRC/out" "$SRC/__aiml_cache" "$SRC/werkzeug"
  (cd "$SRC" && git init -q && git add -A \
     && git -c user.email=t@e -c user.name=t commit -qm one \
     && git push -q "$REPO" HEAD:refs/heads/main) > /dev/null 2>&1
  SHA=$(git -C "$SRC" rev-parse HEAD)
  V4="$WORK/v4"; "$AIML" venv "$V4" > /dev/null 2>&1
  out=$("$AIML" venv "$V4" install werkzeug --git "$REPO" --ref main 2>&1); rc=$?
  check "install --git clones, builds and places it" '[ $rc -eq 0 ] && [ -x "$V4/bin/werkzeug" ]'
  [ $rc -eq 0 ] || echo "$out" | tail -4
  check "the record pins the resolved commit (F68-50)" \
    'grep -q "$SHA" "$V4/lib/aiml/aiml.installed.json"'
  check "the clone lives in the cache, not in the venv (F68-52)" \
    '[ -d "$WORK/cache/venv_tools/werkzeug/$SHA" ] && [ ! -e "$V4/src" ]'
  out=$("$AIML" venv "$V4" install werkzeug --git "$REPO" --ref main 2>&1); rc=$?
  check "the same commit again is a real no-op (F68-57)" \
    '[ $rc -eq 0 ] && echo "$out" | grep -qi "already"'
  out=$("$AIML" venv "$V4" install werkzeug --git "$REPO" --ref gibtsnicht 2>&1); rc=$?
  check "an unknown ref is refused, named" '[ $rc -ne 0 ] && echo "$out" | grep -q gibtsnicht'
  out=$("$AIML" venv "$V4" list 2>&1)
  check "list shows the commit for a git install" 'echo "$out" | grep -q "$SHA"'
else
  echo "  skip git install (no git)"
fi

echo "── §6.1: kontor as a source ──"
# kontor's read side is served by kontor_stub.aiml out of a tree in kontor's
# shape, so this needs no kontor — and tests against the real routes.
SRC="$WORK/src"; mkdir -p "$SRC/werkzeug-0.2.0/lib"
cp "$TOOL/hallo.aiml" "$TOOL/exodus.json" "$SRC/werkzeug-0.2.0/"
cp "$TOOL/lib/greet.aiml" "$SRC/werkzeug-0.2.0/lib/"
printf '{ "name": "werkzeug", "version": "0.2.0", "programs": ["werkzeug"], "package": "lib" }\n' \
  > "$SRC/werkzeug-0.2.0/aiml.tool.json"
cp -r "$SRC/werkzeug-0.2.0" "$SRC/werkzeug-0.1.0"
sed -i 's/0\.2\.0/0.1.0/' "$SRC/werkzeug-0.1.0/aiml.tool.json"
pack() { tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -C "$1" -czf "$2" "$3"; }

SRV="$WORK/kontor"
for v in 0.1.0 0.2.0; do
    mkdir -p "$SRV/exodus/werkzeug/$v"
    cp "$TOOL/exodus.json" "$SRV/exodus/werkzeug/$v/exodus.json"
    pack "$SRC" "$SRV/exodus/werkzeug/$v/package" "werkzeug-$v"
done
# a project in kontor that is no aiml tool: no aiml.tool.json in its archive
mkdir -p "$SRV/exodus/fremd/1.0.0" "$SRC/fremd-1.0.0"
printf 'just text\n' > "$SRC/fremd-1.0.0/README"
cp "$TOOL/exodus.json" "$SRV/exodus/fremd/1.0.0/exodus.json"
pack "$SRC" "$SRV/exodus/fremd/1.0.0/package" "fremd-1.0.0"
# an archive that is no single tree
mkdir -p "$SRV/exodus/zweibaum/1.0.0" "$SRC/zwei/a" "$SRC/zwei/b"
cp "$TOOL/exodus.json" "$SRV/exodus/zweibaum/1.0.0/exodus.json"
tar --sort=name --mtime=@0 -C "$SRC/zwei" -czf "$SRV/exodus/zweibaum/1.0.0/package" a b
mkdir -p "$SRV/api/v1"
cat > "$SRV/api/v1/graph" <<'EOT'
{"projects":[{"name":"werkzeug","version":"0.2.0","dependencies":[],"apt_packages":[]},
             {"name":"fremd","version":"1.0.0","dependencies":[],"apt_packages":[]}]}
EOT

PORT=$((18600 + RANDOM % 300))
KONTOR_STUB_DIR="$SRV" KONTOR_STUB_TOKEN=stub-writer AIML_MEM_V3=1 "$AIML" tests/venv/kontor_stub.aiml -- "$PORT" \
  > "$WORK/stub.log" 2>&1 &
STUB=$!
trap 'kill $STUB 2>/dev/null; rm -rf "$WORK"' EXIT
URL="http://127.0.0.1:$PORT"
up=0
for _ in $(seq 100); do
    curl -fs -o /dev/null "$URL/api/v1/graph" && { up=1; break; }
    sleep 0.2
done
if [ "$up" -ne 1 ]; then
    bad "the kontor stub comes up"
    head -5 "$WORK/stub.log"
else
    ok "the kontor stub comes up"
    V6="$WORK/v6"; "$AIML" venv "$V6" > /dev/null 2>&1
    out=$("$AIML" venv "$V6" install werkzeug --kontor "$URL" 2>&1); rc=$?
    check "install --kontor takes the newest version from the graph (F68-61)" \
      '[ $rc -eq 0 ] && [ -x "$V6/bin/werkzeug" ] && grep -q "\"version\": \"0.2.0\"" "$V6/lib/aiml/aiml.installed.json"'
    check "the package landed and the program runs out of the venv" \
      '[ -f "$V6/lib/aiml/werkzeug/greet.aiml" ] && env -i "$V6/bin/werkzeug" 2>&1 | grep -q "werkzeug laeuft"'
    check "the record says kontor and the digest of the bytes (F68-65)" \
      'grep -q "\"source\": \"kontor:$URL\"" "$V6/lib/aiml/aiml.installed.json" && grep -q "sha256:" "$V6/lib/aiml/aiml.installed.json"'
    out=$("$AIML" venv "$V6" list 2>&1)
    check "list shows the digest and does not call it unreproducible (F68-65)" \
      'echo "$out" | grep -q "sha256:" && ! echo "$out" | grep -qi "unreprod"'
    check "nothing was fetched into the venv (F68-52)" \
      '[ -f "$WORK/cache/venv_tools/werkzeug/kontor-0.2.0/package.tar.gz" ] && [ -z "$(find "$V6" -name "*.tar.gz")" ]'
    out=$("$AIML" venv "$V6" install werkzeug --kontor "$URL" 2>&1); rc=$?
    check "the same digest again is a no-op (F68-57/65)" \
      '[ $rc -eq 0 ] && echo "$out" | grep -qi "already"'
    out=$("$AIML" venv "$V6" install werkzeug --kontor "$URL" --version 0.1.0 2>&1); rc=$?
    check "--version names one, and it replaces (F68-61/57)" \
      '[ $rc -eq 0 ] && grep -q "\"version\": \"0.1.0\"" "$V6/lib/aiml/aiml.installed.json"'
    out=$("$AIML" venv "$V6" install werkzeug --kontor "$URL" --version 9.9.9 2>&1); rc=$?
    check "a version kontor does not have is refused with its status (F68-62)" \
      '[ $rc -ne 0 ] && echo "$out" | grep -q "404"'
    out=$("$AIML" venv "$V6" install zweibaum --kontor "$URL" --version 1.0.0 2>&1); rc=$?
    check "an archive with two top-level entries is refused (F68-62)" \
      '[ $rc -ne 0 ] && echo "$out" | grep -q "one directory"'
    out=$("$AIML" venv "$V6" install nixda --kontor "$URL" 2>&1); rc=$?
    check "a project kontor does not know is refused, named (F68-61)" \
      '[ $rc -ne 0 ] && echo "$out" | grep -q "nixda"'

    V7="$WORK/v7"; "$AIML" venv "$V7" > /dev/null 2>&1
    out=$("$AIML" venv "$V7" install --all --kontor "$URL" 2>&1); rc=$?
    check "install --all installs the tools (F68-66)" \
      '[ $rc -eq 0 ] && [ -x "$V7/bin/werkzeug" ]'
    check "and skips what is no aiml tool, with a line (F68-66)" \
      'echo "$out" | grep -q "fremd" && echo "$out" | grep -q "aiml.tool.json"'
    check "and counts what it did" 'echo "$out" | grep -q "1 installed, 1 skipped, 0 failed"'

    echo "── §6.3: publish to kontor ──"
    if command -v git > /dev/null; then
        PUB="$WORK/pub"; cp -r "$TOOL" "$PUB"
        sed -i 's/"werkzeug"/"pubtool"/; s/"0\.3\.0"/"1.0.0"/' "$PUB/exodus.json"
        printf '{ "name": "pubtool", "version": "1.0.0", "programs": ["pubtool"], "package": "lib" }\n' \
          > "$PUB/aiml.tool.json"
        git -C "$PUB" init -q
        git -C "$PUB" add -A
        git -C "$PUB" -c user.name=t -c user.email=t@t commit -qm v1
        echo "scratch" > "$PUB/untracked.txt"
        pub() { env KONTOR_TOKEN="${1}" "$AIML" venv "$V6" publish --path "$PUB" --kontor "$URL" 2>&1; }

        out=$(env -u KONTOR_TOKEN "$AIML" venv "$V6" publish --path "$PUB" --kontor "$URL" 2>&1); rc=$?
        check "publish without \$KONTOR_TOKEN is refused, named (F68-74)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q KONTOR_TOKEN'
        out=$(pub wrong); rc=$?
        check "a token kontor does not take: the status and kontor's word (F68-74)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q "401"'
        out=$(KONTOR_TOKEN=stub-writer "$AIML" venv "$V6" publish nicht-pubtool --path "$PUB" --kontor "$URL" 2>&1); rc=$?
        check "a name that is not the manifest's is refused (F68-70)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q "pubtool"'
        sed -i 's/1\.0\.0/1.0.9/' "$PUB/exodus.json"
        out=$(pub stub-writer); rc=$?
        check "exodus.json and aiml.tool.json must agree on the version (F68-71)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q "1.0.9"'
        git -C "$PUB" checkout -q exodus.json
        echo "# changed" >> "$PUB/hallo.aiml"
        out=$(pub stub-writer); rc=$?
        check "uncommitted changes are refused (F68-72)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q "uncommitted"'
        git -C "$PUB" checkout -q hallo.aiml

        out=$(pub stub-writer); rc=$?
        check "publish puts manifest and package where install looks (F68-71)" \
          '[ $rc -eq 0 ] && [ -f "$SRV/exodus/pubtool/1.0.0/exodus.json" ] && [ -f "$SRV/exodus/pubtool/1.0.0/package" ]'
        [ $rc -eq 0 ] || echo "$out" | tail -3
        check "the manifest is the repository's exodus.json (F68-71)" \
          'cmp -s "$PUB/exodus.json" "$SRV/exodus/pubtool/1.0.0/exodus.json"'
        lst=$(tar -tzf "$SRV/exodus/pubtool/1.0.0/package" 2>/dev/null)
        check "the package is one tree <name>-<version>/ of what git tracks (F68-72)" \
          'echo "$lst" | grep -qx "pubtool-1.0.0/aiml.tool.json" && echo "$lst" | grep -qx "pubtool-1.0.0/lib/greet.aiml" && ! echo "$lst" | grep -q untracked && [ -z "$(echo "$lst" | grep -v "^pubtool-1.0.0/")" ]'
        check "the line names the digest of the bytes kontor got" \
          'echo "$out" | grep -q "sha256:$(sha256sum "$SRV/exodus/pubtool/1.0.0/package" | cut -d" " -f1)"'
        check "no staging left inside the venv (F68-52)" '[ -z "$(find "$V6" -name "*.tar*")" ]'

        cp "$SRV/exodus/pubtool/1.0.0/package" "$WORK/first.pkg"
        out=$(pub stub-writer); rc=$?
        check "a published version is not replaced (F68-73)" \
          '[ $rc -ne 0 ] && echo "$out" | grep -q "already" && cmp -s "$WORK/first.pkg" "$SRV/exodus/pubtool/1.0.0/package"'

        sed -i 's/1\.0\.0/1.0.1/' "$PUB/exodus.json" "$PUB/aiml.tool.json"
        git -C "$PUB" -c user.name=t -c user.email=t@t commit -qam v1.0.1
        out=$(pub stub-writer); rc=$?
        check "a new version is published next to it" \
          '[ $rc -eq 0 ] && [ -f "$SRV/exodus/pubtool/1.0.1/package" ]'
        rm -rf "$WORK/x1" "$WORK/x2"; mkdir -p "$WORK/x1" "$WORK/x2"
        tar -xzf "$WORK/first.pkg" -C "$WORK/x1"; tar -xzf "$SRV/exodus/pubtool/1.0.1/package" -C "$WORK/x2"
        check "with the same layout (only the version differs)" \
          'diff -r "$WORK/x1/pubtool-1.0.0/lib" "$WORK/x2/pubtool-1.0.1/lib" > /dev/null'

        V8="$WORK/v8"; "$AIML" venv "$V8" > /dev/null 2>&1
        out=$("$AIML" venv "$V8" install pubtool --kontor "$URL" --version 1.0.0 2>&1); rc=$?
        check "and what was published installs from there (F68-64)" \
          '[ $rc -eq 0 ] && [ -x "$V8/bin/pubtool" ] && env -i "$V8/bin/pubtool" 2>&1 | grep -q "werkzeug laeuft"'
        [ $rc -eq 0 ] || echo "$out" | tail -3
    else
        echo "  skip publish (no git)"
    fi
    kill $STUB 2>/dev/null
fi

[ $fail -eq 0 ] && echo "venv: PASS" || echo "venv: FAIL"
exit $fail
