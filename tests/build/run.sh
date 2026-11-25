#!/usr/bin/env bash
# run.sh — `aiml build` (spec/68 §7): an aiml project built from its exodus project file by the
# compiler itself, without exodus and without Python.
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
export AIML_CACHE="$WORK/cache" AIML_MEM_V3=1

VENV="$WORK/v"
"$AIML" venv "$VENV" > /dev/null 2>&1 || { echo "cannot make a venv"; exit 1; }
# No exodus and no Python on PATH: what the build needs is the venv and a linker.
CLEANPATH="$VENV/bin:$(dirname "$(command -v g++)")"
for p in /usr/bin /bin; do case ":$CLEANPATH:" in *":$p:"*) ;; *) CLEANPATH="$CLEANPATH:$p";; esac; done
NOPY="$WORK/nopy"; mkdir -p "$NOPY"
for t in python3 python exodus; do printf '#!/bin/sh\necho "%s must not run" >&2; exit 99\n' "$t" > "$NOPY/$t"; chmod +x "$NOPY/$t"; done
inv() { (cd "$1" && shift && env -i HOME="$WORK" AIML_CACHE="$AIML_CACHE" AIML_MEM_V3=1 \
          PATH="$NOPY:$CLEANPATH" AIML_VENV="$VENV" AIML="$VENV/bin/aiml" "$VENV/bin/aiml" build "$@" 2>&1); }

P="$WORK/proj"; mkdir -p "$P/lib" "$P/sub" "$P/hooks"
cat > "$P/lib/greet.aiml" <<'EOT'
- def:
    name: greet
    return: i32
    body:
      - print: ["aus dem modul"]
      - return: 7
EOT
cat > "$P/main.aiml" <<'EOT'
- import: "lib/greet.aiml"
- def:
    name: main
    return: i32
    body:
      - print: ["hallo build"]
      - return: { call: [greet] }
EOT
cat > "$P/sub/other.aiml" <<'EOT'
- def:
    name: main
    return: i32
    body:
      - print: ["aus sub"]
      - return: 0
EOT
cat > "$P/offer.aiml" <<'EOT'
- def:
    name: offer_value
    return: i32
    body:
      - return: 5
EOT
cfg() {  # cfg <file> <name> <output_type> <source> [extra json, with leading comma]
local envj="${ENVJ:-}" aic="${AIC:-true}"
[ -n "$envj" ] || envj='{ "AIML_MEM_V3": "1" }'
cat > "$1" <<EOT
{
    "\$schema": "exodus.project.config-1.0",
    "name": "$2", "version": "0.1.0",
    "output_type": "$3",
    "sources": ["$4"],
    "compiler": { "name": "\$AIML", "lang_standard": "17", "flags": [] },
    "linker": { "name": "g++", "flags": ["-Wl,-rpath,\$ORIGIN/../lib"],
                "libraries": ["aiml", "pthread", "dl"],
                "library_paths": ["\$AIML_VENV/lib"] },
    "artifact_in_cwd": $aic, "optimization": "2", "debug": false, "jobs": 4,
    "env": $envj${5:-}
}
EOT
}
cfg "$P/exodus.json" hallo executable main.aiml
cfg "$P/exodus-lib.json" offer shared_lib offer.aiml
AIC=false cfg "$P/sub/exodus-other.json" other executable sub/other.aiml
printf '{ "name": "not a build file" }\n' > "$P/aiml.tool.json"

echo "── aiml build (spec/68 §7) ──"
out=$(inv "$P"); rc=$?
check "builds ./exodus.json without -c (F68-80)" '[ $rc -eq 0 ] && [ -x "$P/hallo" ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "the program runs, the imported module is linked in (F68-83)" \
  'o=$("$P/hallo" 2>&1); r=$?; [ $r -eq 7 ] && echo "$o" | grep -q "hallo build" && echo "$o" | grep -q "aus dem modul"'
check "objects live in out/<name>/ (F68-83)" '[ -f "$P/out/hallo/main.o" ] && ls "$P/out/hallo/" | grep -q "_aiml.o"'
check "RUNPATH starts with \$ORIGIN/../lib (F68-84)" '[[ "$(runpath "$P/hallo")" == "\$ORIGIN/../lib"* ]]'
check "and the venv's lib follows as rpath entry (F68-84)" '[[ "$(runpath "$P/hallo")" == *"$VENV/lib"* ]]'
check "each command is printed (F68-86)" 'echo "$out" | grep -q "g++" && echo "$out" | grep -q "main.aiml"'
check "neither exodus nor Python ran" '! echo "$out" | grep -q "must not run"'

out=$(inv "$P" -c exodus-lib.json -c sub/exodus-other.json); rc=$?
check "-c twice builds both, in order (F68-80)" '[ $rc -eq 0 ] && [ -f "$P/liboffer.so" ] && [ -x "$P/out/other/other" ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "shared_lib: lib<name>.so, a shared object (F68-84)" 'file "$P/liboffer.so" | grep -q "shared object"'
check "artifact_in_cwd false: the output stays in out/<name>/ (F68-84)" '[ ! -e "$P/other" ]'
check "a config in a subdirectory: paths from the working directory (F68-80)" '"$P/out/other/other" | grep -q "aus sub"'

rm -f "$P/hallo" "$P/liboffer.so"
out=$(inv "$P" --all); rc=$?
check "--all builds what carries the \$schema, nothing else (F68-80)" \
  '[ $rc -eq 0 ] && [ -x "$P/hallo" ] && [ -f "$P/liboffer.so" ] && ! echo "$out" | grep -q "aiml.tool.json"'

echo "── stale objects, env, placeholders ──"
touch "$P/out/hallo/stale_aiml.o"
out=$(inv "$P"); rc=$?
check "the build directory is emptied first (F68-83)" '[ $rc -eq 0 ] && [ ! -e "$P/out/hallo/stale_aiml.o" ]'
ENVJ='{ "AIML_MEM_V3": "1", "AIML_PROBE": "$AIML_VENV" }' cfg "$P/exodus-env.json" envprobe executable main.aiml ', "post_linkage": "hooks/env.aiml"'
cat > "$P/hooks/env.aiml" <<'EOT'
- def:
    name: main
    return: i32
    body:
      - decl: [vars, map<string:string>, { call: [sys.env] }]
      - decl: [seen, string, { call: [vars.get, "AIML_PROBE"] }]
      - call: [io.write_file, "envseen.txt", seen]
      - return: 0
EOT
out=$(inv "$P" -c exodus-env.json); rc=$?
check "env reaches the build, with \$AIML_VENV expanded (F68-85)" '[ $rc -eq 0 ] && [ "$(cat "$P/envseen.txt" 2>/dev/null)" = "$VENV" ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
out=$(cd "$P" && env -i HOME="$WORK" PATH="$NOPY:/usr/bin:/bin" AIML_CACHE="$AIML_CACHE" "$AIML" build 2>&1); rc=$?
check "an unresolvable \$AIML_VENV ends the build, naming . <venv>/env (F68-85)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "env"'

echo "── hooks (F68-87) ──"
H="$WORK/hooked"; mkdir -p "$H/hooks"; cp -r "$P/lib" "$H/"
cat > "$H/main.aiml" <<'EOT'
- import: "gen/stamp.aiml"
- def:
    name: main
    return: i32
    body:
      - return: { call: [stamp] }
EOT
# hook <stage>: writes what it saw into hooks/<stage>.seen — its variables, and whether the
# object (main.o) and the linked output exist at that moment.
hook() {
cat > "$H/hooks/$1.aiml" <<EOT
- def:
    name: main
    return: i32
    body:
      - decl: [vars, map<string:string>, { call: [sys.env] }]
      - decl: [stage, string, { call: [vars.get, "AIML_BUILD_STAGE"] }]
      - decl: [name, string, { call: [vars.get, "AIML_BUILD_NAME"] }]
      - decl: [conf, string, { call: [vars.get, "AIML_BUILD_CONFIG"] }]
      - decl: [bdir, string, { call: [vars.get, "AIML_BUILD_DIR"] }]
      - decl: [outp, string, { call: [vars.get, "AIML_BUILD_OUTPUT"] }]
      - decl: [obj, string, "no-obj"]
      - if: { condition: { call: [io.exists, "\${bdir}/main.o"] }, then: [{ set: [obj, "obj"] }] }
      - decl: [linked, string, "no-out"]
      - if: { condition: { call: [io.exists, outp] }, then: [{ set: [linked, "out"] }] }
      - call: [io.write_file, "hooks/$1.seen", "\${stage}|\${name}|\${conf}|\${bdir}|\${outp}|\${obj}|\${linked}"]
$2
      - return: 0
EOT
}
hook pre_compilation '      - call: [io.mkdir_all, "gen"]
      - call: [io.write_file, "gen/stamp.aiml", "- def:\n    name: stamp\n    return: i32\n    body:\n      - return: 3\n"]'
hook pre_linkage ''
hook post_linkage ''
cfg "$H/exodus.json" hooked executable main.aiml \
  ', "pre_compilation": "hooks/pre_compilation.aiml", "pre_linkage": "hooks/pre_linkage.aiml", "post_linkage": "hooks/post_linkage.aiml"'
out=$(inv "$H"); rc=$?
check "the three hooks run and the build succeeds" '[ $rc -eq 0 ] && [ -x "$H/hooked" ]'
[ $rc -eq 0 ] || echo "$out" | tail -8
check "pre_compilation writes a file the compile sees" '"$H/hooked"; [ $? -eq 3 ]'
seen() { cat "$H/hooks/$1.seen" 2>/dev/null; }
check "pre_compilation: before any object, its variables set" \
  '[ "$(seen pre_compilation)" = "pre_compilation|hooked|exodus.json|$H/out/hooked|$H/hooked|no-obj|no-out" ]'
check "pre_linkage: after the compile, before the link" \
  '[ "$(seen pre_linkage)" = "pre_linkage|hooked|exodus.json|$H/out/hooked|$H/hooked|obj|no-out" ]'
check "post_linkage: after the link" \
  '[ "$(seen post_linkage)" = "post_linkage|hooked|exodus.json|$H/out/hooked|$H/hooked|obj|out" ]'
check "hooks ran with the JIT, not Python" '! echo "$out" | grep -q "must not run"'

cat > "$H/hooks/fails.aiml" <<'EOT'
- def:
    name: main
    return: i32
    body:
      - call: [sys.exit, 4]
      - return: 4
EOT
cfg "$H/exodus-f.json" hookfail executable main.aiml ', "pre_linkage": "hooks/fails.aiml"'
out=$(inv "$H" -c exodus-f.json); rc=$?
check "a failing hook ends the build, naming hook and stage, no output" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "fails.aiml" && echo "$out" | grep -q "pre_linkage" && [ ! -e "$H/hookfail" ]'
cfg "$H/exodus-m.json" hookmiss executable main.aiml ', "pre_compilation": "hooks/nicht-da.aiml"'
out=$(inv "$H" -c exodus-m.json); rc=$?
check "a missing hook is an error, named" '[ $rc -ne 0 ] && echo "$out" | grep -q "nicht-da.aiml"'
echo 'def run(config): pass' > "$H/hooks/old.py"
cfg "$H/exodus-py.json" hookpy executable main.aiml ', "pre_compilation": "hooks/old.py"'
out=$(inv "$H" -c exodus-py.json); rc=$?
check "a .py hook is refused: port it to an .aiml hook" '[ $rc -ne 0 ] && echo "$out" | grep -q "\.aiml hook"'

echo "── refused: needs exodus (F68-82) ──"
R="$WORK/refuse"; mkdir -p "$R"; cp "$P/main.aiml" "$R/"; cp -r "$P/lib" "$R/"
refused() {  # refused <label> <extra json> [output_type] [source]
    cfg "$R/exodus-x.json" x "${3:-executable}" "${4:-main.aiml}" "$2"
    out=$(inv "$R" -c exodus-x.json); rc=$?
    check "$1 is refused: needs exodus" '[ $rc -ne 0 ] && echo "$out" | grep -q "needs exodus"'
}
refused "output_type wasm" '' wasm
refused "output_type static_lib" '' static_lib
echo 'int x;' > "$R/c.cpp"
refused "a C++ source" '' executable c.cpp
refused "apt_packages" ', "apt_packages": [{ "name": "zlib1g-dev", "version": "1" }]'
refused "git_packages" ', "git_packages": [{ "name": "emsdk", "url": "x" }]'
ENVJ='{ "X": "__exodus_cache/apt" }' refused "a path into __exodus_cache" ''

echo "── failures ──"
F="$WORK/broken"; mkdir -p "$F"
printf -- '- def:\n    name: main\n    return: i32\n    body:\n      - return: { call: [nicht_da] }\n' > "$F/main.aiml"
cfg "$F/exodus.json" broken executable main.aiml
out=$(inv "$F"); rc=$?
check "a compile error ends the build, naming the configuration (F68-86)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus.json" && [ ! -e "$F/broken" ]'
cp "$P/main.aiml" "$F/"; cp -r "$P/lib" "$F/"
cfg "$F/exodus.json" broken executable main.aiml
sed -i 's/"name": "g++"/"name": "kein-linker-da"/' "$F/exodus.json"
out=$(inv "$F"); rc=$?
check "a missing linker is named (F68-84)" '[ $rc -ne 0 ] && echo "$out" | grep -q "kein-linker-da"'
out=$(inv "$WORK"); rc=$?
check "no exodus.json: refused, named" '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus.json"'

[ $fail -eq 0 ] && echo "build: PASS" || echo "build: FAIL"
exit $fail
