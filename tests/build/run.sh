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
local cvj=""; [ -z "${CV:-}" ] || cvj=", \"version\": $CV"
[ -n "$envj" ] || envj='{ "AIML_MEM_V3": "1" }'
cat > "$1" <<EOT
{
    "\$schema": "exodus.project.config-1.0",
    "name": "$2", "version": "0.1.0",
    "output_type": "$3",
    "sources": ["$4"],
    "compiler": { "name": "\$AIML", "lang_standard": "17", "flags": []$cvj },
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

echo "── wasm (F68-88, F68-89) ──"
W="$WORK/wasm"; mkdir -p "$W/assets" "$W/fakeem" "$W/rt"; cp "$P/main.aiml" "$W/"; cp -r "$P/lib" "$W/"
echo "shader" > "$W/assets/a.txt"
# A stand-in emcc: names a version, writes down its arguments, leaves the three outputs.
cat > "$W/fakeem/emcc" <<'EOT'
#!/bin/sh
if [ "$1" = "--version" ]; then echo "emcc (Emscripten gcc/clang-like replacement) ${FAKE_EM_VERSION:-3.1.74} (abc)"; exit 0; fi
printf '%s\n' "$@" > "$FAKE_EM_LOG"
out=""; prev=""
for a in "$@"; do [ "$prev" = "-o" ] && out="$a"; prev="$a"; done
b="${out%.html}"; : > "$out"; : > "$b.js"; : > "$b.wasm"
EOT
chmod +x "$W/fakeem/emcc"
: > "$W/rt/libaiml_wasm.a"; echo "3.1.74" > "$W/rt/emsdk.version"
wcfg() {  # wcfg <file> <name> <source> [extra json, with leading comma]
cat > "$1" <<EOT
{
    "\$schema": "exodus.project.config-1.0",
    "name": "$2", "version": "0.1.0", "output_type": "wasm",
    "sources": ["$3"],
    "asset_directories": ["assets"],
    "compiler": { "name": "\$AIML" },
    "linker": { "flags": ["-fexceptions", "-sASYNCIFY=1", "-sALLOW_MEMORY_GROWTH=1", "-sENVIRONMENT=web,node"],
                "libraries": ["aiml_wasm"], "library_paths": ["$W/rt"] },
    "artifact_in_cwd": true,
    "env": { "AIML_MEM_V3": "1", "FAKE_EM_LOG": "$W/emcc.args" }${4:-}
}
EOT
}
wcfg "$W/exodus.json" probe main.aiml
winv() { (cd "$W" && env -i HOME="$WORK" AIML_CACHE="$AIML_CACHE" AIML_MEM_V3=1 FAKE_EM_VERSION="${FAKE_EM_VERSION:-3.1.74}" \
          PATH="$NOPY:$W/fakeem:$CLEANPATH" AIML_VENV="$VENV" AIML="$VENV/bin/aiml" "$VENV/bin/aiml" build "$@" 2>&1); }
out=$(winv); rc=$?
check "output_type wasm builds with emcc from PATH (F68-88)" '[ $rc -eq 0 ] && [ -f "$W/probe.html" ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "the compile targets wasm32 with a main (F68-88)" 'echo "$out" | grep -q "main.aiml -o $W/out/probe/main.ll .*--target wasm32 --emit-main"'
check "the module and its import are linked as .ll (F68-88)" \
  'grep -qx "$W/out/probe/main.ll" "$W/emcc.args" && grep -q "_aiml\.ll$" "$W/emcc.args" && ! grep -q "\.o$" "$W/emcc.args"'
check "assets become --preload-file (F68-88)" 'grep -qx -- "--preload-file=assets" "$W/emcc.args"'
check "flags, -L and -l as given, no rpath (F68-88)" \
  'grep -qx -- "-sASYNCIFY=1" "$W/emcc.args" && grep -qx -- "-L$W/rt" "$W/emcc.args" && grep -qx -- "-laiml_wasm" "$W/emcc.args" && ! grep -q rpath "$W/emcc.args"'
out=$(FAKE_EM_VERSION=4.0.1 winv); rc=$?
check "an emcc of another emsdk than the runtime's is refused, both named (F68-89)" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "3.1.74" && echo "$out" | grep -q "4.0.1"'
printf 'int x;\n' > "$W/x.c"; cc -c "$W/x.c" -o "$W/x.o" 2>/dev/null
wcfg "$W/exodus-o.json" probe_o x.o
out=$(winv -c exodus-o.json); rc=$?
check "a native object in a wasm build is refused (F68-88)" '[ $rc -ne 0 ] && echo "$out" | grep -q "native object"'
wcfg "$W/exodus-g.json" probe_g main.aiml ', "git_packages": [{ "name": "emsdk", "url": "x" }]'
out=$(winv -c exodus-g.json); rc=$?
check "git_packages stay refused in a wasm build: emsdk is the environment's (F68-82)" '[ $rc -ne 0 ] && echo "$out" | grep -q "needs exodus"'
# The real thing where the CI has an emsdk and the wasm32 runtime (tools/run_wasm_test.sh's places).
EMDIR=$(ls -d "$ROOT"/__exodus_cache/git/emsdk/*/upstream/emscripten 2>/dev/null | head -1)
NODE=$(ls "$ROOT"/__exodus_cache/git/emsdk/*/node/*/bin/node 2>/dev/null | head -1)
RTW="$ROOT/out/aiml_wasm/aiml_wasm"
if [ -n "$EMDIR" ] && [ -n "$NODE" ] && [ -f "$RTW/libaiml_wasm.a" ]; then
    R2="$WORK/wasmreal"; mkdir -p "$R2/assets" "$R2/rt"; cp "$P/main.aiml" "$R2/"; cp -r "$P/lib" "$R2/"; : > "$R2/assets/a.txt"
    cp "$RTW/libaiml_wasm.a" "$R2/rt/"
    W="$R2" wcfg "$R2/exodus.json" real main.aiml
    # No $NOPY here: emcc itself is a Python program.  That aiml build runs no
    # Python is what the fake-emcc checks above already show.
    out=$(cd "$R2" && env -i HOME="$WORK" AIML_CACHE="$AIML_CACHE" AIML_MEM_V3=1 EM_CONFIG="${EMDIR%/upstream/emscripten}/.emscripten" \
          PATH="$EMDIR:$(dirname "$NODE"):$CLEANPATH" AIML_VENV="$VENV" AIML="$VENV/bin/aiml" "$VENV/bin/aiml" build 2>&1); rc=$?
    check "with a real emsdk: real.html, real.js, real.wasm (F68-88)" '[ $rc -eq 0 ] && [ -f "$R2/real.js" ] && [ -f "$R2/real.wasm" ]'
    [ $rc -eq 0 ] || echo "$out" | tail -8
    o=$(cd "$R2" && "$NODE" real.js 2>&1); r=$?
    check "and node runs it: the module's line and its exit code" '[ $r -eq 7 ] && echo "$o" | grep -q "hallo build" && echo "$o" | grep -q "aus dem modul"'
else
    echo "  skip a real wasm build (no emsdk in __exodus_cache or no out/aiml_wasm)"
fi

echo "── refused: needs exodus (F68-82) ──"
R="$WORK/refuse"; mkdir -p "$R"; cp "$P/main.aiml" "$R/"; cp -r "$P/lib" "$R/"
refused() {  # refused <label> <extra json> [output_type] [source]
    cfg "$R/exodus-x.json" x "${3:-executable}" "${4:-main.aiml}" "$2"
    out=$(inv "$R" -c exodus-x.json); rc=$?
    check "$1 is refused: needs exodus" '[ $rc -ne 0 ] && echo "$out" | grep -q "needs exodus"'
}
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

# ── §7.2 version pins (F68-95…F68-99) ──────────────────────────────────────────
echo "── version pins (spec/68 §7.2) ──"
REL="$WORK/rel"; mkdir -p "$REL/bin" "$REL/lib"
cp "$VENV/bin/aiml" "$REL/bin/aiml"; cp "$VENV/lib/libaiml.so" "$REL/lib/libaiml.so"
printf 'v1.0.2.7\n' > "$REL/lib/aiml.version"
VR="$WORK/vr"; "$REL/bin/aiml" venv "$VR" > /dev/null 2>&1
check "aiml.version next to the runtime comes along into the venv (F68-98)" \
  'cmp -s "$REL/lib/aiml.version" "$VR/lib/aiml.version"'
check "aiml --version names the release (F68-98)" '[ "$("$VR/bin/aiml" --version)" = "aiml v1.0.2.7" ]'
check "a build-tree compiler says it is unreleased (F68-98)" '"$AIML" --version | grep -q "unreleased"'
invr() { (cd "$1" && shift && env -i HOME="$WORK" AIML_CACHE="$AIML_CACHE" AIML_MEM_V3=1 \
          PATH="$NOPY:$VR/bin:${CLEANPATH#*:}" AIML_VENV="$VR" AIML="$VR/bin/aiml" "$VR/bin/aiml" build "$@" 2>&1); }
V="$WORK/pins"; mkdir -p "$V"; cp "$P/main.aiml" "$V/"; cp -r "$P/lib" "$V/"
pin() {  # pin <compiler.version json> [dependencies json]
    CV="$1" cfg "$V/exodus.json" pinned executable main.aiml ", \"dependencies\": ${2:-[]}"
}
pin '{ "ge": "1.0.2.7", "lt": "1.1" }'
out=$(invr "$V"); rc=$?
check "a pin that admits the compiler builds without a line (F68-99)" \
  '[ $rc -eq 0 ] && [ -x "$V/pinned" ] && ! echo "$out" | grep -q "version-mismatch"'
pin '{ "eq": "1.0.2.7" }'
out=$(invr "$V"); rc=$?
check "eq: v1.0.2.7 equals 1.0.2.7 (F68-97)" '[ $rc -eq 0 ] && ! echo "$out" | grep -q "version-mismatch"'
pin '{ "gt": "1.0.2.7" }'
out=$(invr "$V"); rc=$?
check "a pin that excludes it: one mismatch line, the build goes on, rc 0 (F68-99)" \
  '[ $rc -eq 0 ] && [ -x "$V/pinned" ] && [ "$(echo "$out" | grep -c "^aiml: version-mismatch compiler aiml v1.0.2.7 not in {gt 1.0.2.7} (exodus.json)")" -eq 1 ]'
pin '{ "lt": "1.0.10" }'
out=$(invr "$V"); rc=$?
check "numeric, not text: 1.0.2.7 is below 1.0.10 (F68-97)" '[ $rc -eq 0 ] && ! echo "$out" | grep -q "version-mismatch"'
pin '{ "ge": "1.0.2.7" }'
out=$(inv "$V"); rc=$?
check "a compiler without aiml.version mismatches every pin (F68-98)" \
  '[ $rc -eq 0 ] && echo "$out" | grep -q "version-mismatch compiler aiml none not in {ge 1.0.2.7}.*not a released compiler"'
pin '{ "ge": "1.0" }' '[ { "name": "ufo", "version": { "eq": "0.1.0" } } ]'
out=$(invr "$V"); rc=$?
check "a dependency that is not installed is a mismatch with that reason (F68-99)" \
  '[ $rc -eq 0 ] && echo "$out" | grep -q "version-mismatch dependency ufo none not in {eq 0.1.0}.*not installed"'
printf '{"tools": {\n  "ufo": {"source": "x", "commit": "", "version": "0.1.0", "files": []}\n}}\n' > "$VR/lib/aiml/aiml.installed.json"
out=$(invr "$V"); rc=$?
check "an installed dependency inside the pin: no line (F68-99)" '[ $rc -eq 0 ] && ! echo "$out" | grep -q "version-mismatch"'
rm -f "$VR/lib/aiml/aiml.installed.json"
for bad in '"1.0.2.7"' '{ "eq": "1.0", "lt": "2" }' '{ "gte": "1.0" }' '{ "gt": "2", "lt": "1" }' '{ "ge": "1.0", "lt": "1.0" }' '{ "eq": "1.0-rc1" }' '{}'; do
    pin "$bad"
    out=$(invr "$V"); rc=$?
    check "refused, naming the pin: $bad (F68-96/F68-97)" \
      '[ $rc -ne 0 ] && echo "$out" | grep -q "version pin of compiler aiml"'
done

[ $fail -eq 0 ] && echo "build: PASS" || echo "build: FAIL"
exit $fail
