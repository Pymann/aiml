#!/usr/bin/env bash
# run.sh — tools/ci/llvm_bump.py, the configuration side of the LLVM update
# workflow (TEST_SUITES.md §4.2): on copies of the real exodus configurations and
# an apt Packages index written here.  The configurations' own LLVM (OLD) is read
# from them and the move goes to OLD+5, so the test holds on whichever LLVM the
# repository is — also inside the workflow, which runs it on configurations it
# has just moved.
set -uo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fail=0
ok()   { echo "  ok   $1"; }
bad()  { echo "  FAIL $1"; fail=1; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
B="python3 $ROOT/tools/ci/llvm_bump.py"

CFGS="exodus.json exodus2.json exodus-test.json exodus-asan.json exodus-wasm-runtime.json"
for f in $CFGS; do cp "$f" "$WORK/"; done
cd "$WORK"
OLD=$($B current exodus.json)
N=$((OLD + 5))
OLDV=$(python3 -c "import json;print(next(p['version'] for p in json.load(open('exodus.json'))['apt_packages'] if p['name']=='llvm-$OLD-dev'))")
NEWV="1:$N.1.8~++20261020082717+0a1b2c3d4e5f-1~exp1~20261020082834.93"
MESA=$(python3 -c "import json;print(next((p['name']+' '+p['version'] for p in json.load(open('exodus.json'))['apt_packages'] if p['name'].startswith('libllvm') and p['name']!='libllvm$OLD'),''))")
idx() {  # idx <file> [skip-package] — an index with the toolchain at N
    : > "$1"
    for p in llvm-$N llvm-$N-dev llvm-$N-runtime llvm-$N-tools llvm-$N-linker-tools libllvm$N libclang-cpp$N; do
        [ "$p" = "${2:-}" ] && continue
        sum=$(printf '%s' "$p" | sha256sum | cut -d' ' -f1)
        printf 'Package: %s\nArchitecture: amd64\nVersion: 1:%s.1.2-1~exp1\nSHA256: 0000\n\n' "$p" "$N" >> "$1"
        printf 'Package: %s\nArchitecture: amd64\nVersion: %s\nDepends: libc6 (>= 2.34),\n libstdc++6\nFilename: pool/main/l/llvm-toolchain-%s/%s.deb\nSHA256: %s\n\n' "$p" "$NEWV" "$N" "$p" "$sum" >> "$1"
    done
}
idx Packages

echo "── llvm_bump.py (TEST_SUITES.md §4.2) ──"
check "current: the configurations name their LLVM ($OLD)" '[ -n "$OLD" ] && [ "$OLD" -ge 18 ]'
cp exodus.json exodus.before
WITH=$(grep -lF -e "llvm-$OLD" -e "\"LLVM-$OLD\"" -e "libllvm$OLD" -e "libclang-cpp$OLD" $CFGS | wc -l)
out=$($B rewrite --major $N --packages Packages $CFGS 2>&1); rc=$?
check "rewrite moves every configuration with LLVM $OLD in it ($WITH, → $N)" '[ $rc -eq 0 ] && [ "$(echo "$out" | grep -c "LLVM $OLD → $N")" -eq "$WITH" ] && [ "$WITH" -ge 3 ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "nothing of LLVM $OLD is left" '! grep -lF -e "llvm-$OLD/" -e "llvm-$OLD\"" -e "llvm-$OLD-" -e "llvm-c-$OLD" -e "\"libllvm$OLD\"" -e "libclang-cpp$OLD" -e "\"LLVM-$OLD\"" -e "$OLDV" $CFGS'
check "the newest build in the index is taken, with its sha256" \
  'python3 -c "import json,sys;p={x[\"name\"]:x for x in json.load(open(\"exodus.json\"))[\"apt_packages\"]};d=p[\"llvm-$N-dev\"];sys.exit(0 if d[\"version\"]==\"$NEWV\" and d[\"digest\"]==\"$(printf llvm-$N-dev | sha256sum | cut -d" " -f1)\" else 1)"'
check "the include paths follow: package, version, llvm-$N and llvm-c-$N" \
  'grep -qF "__exodus_cache/apt/llvm-$N-dev/amd64/$NEWV/payload/usr/include/llvm-$N\"" exodus.json && grep -qF "__exodus_cache/apt/llvm-$N-dev/amd64/$NEWV/payload/usr/include/llvm-c-$N\"" exodus.json'
check "the library paths follow" 'grep -qF "__exodus_cache/apt/libllvm$N/amd64/$NEWV/payload/usr/lib/llvm-$N/lib\"" exodus2.json'
check "the library LLVM-$OLD becomes LLVM-$N" 'grep -q "\"LLVM-$N\"" exodus2.json'
if [ -n "$MESA" ]; then
    check "${MESA% *} (Mesa's, another major) is not touched" \
      'grep -qF "\"name\": \"${MESA% *}\"" exodus.json && grep -qF "apt/${MESA% *}/amd64/${MESA#* }/payload" exodus.json'
fi
check "no package is lost or gained" \
  '[ "$(python3 -c "import json;print(len(json.load(open(\"exodus.json\"))[\"apt_packages\"]))")" = "$(python3 -c "import json;print(len(json.load(open(\"exodus.before\"))[\"apt_packages\"]))")" ]'
check "everything else is as it was (the diff is the bump)" \
  '[ -z "$(diff <(grep -v -i "llvm\|clang-cpp\|\"digest\"\|\"version\": \"1:" exodus.before) <(grep -v -i "llvm\|clang-cpp\|\"digest\"\|\"version\": \"1:" exodus.json))" ]'
cp "$ROOT/exodus-aot-repro.json" aot.json; cp aot.json aot.before; cp "$ROOT/exodus.json" fresh.json
out=$($B rewrite --major $N --packages Packages fresh.json aot.json 2>&1)
check "a configuration without LLVM is not written at all" 'cmp -s aot.json aot.before && echo "$out" | grep -q "aot.json: no LLVM $OLD in it"'
check "the clang macros of the wasm runtime stay" 'grep -q "__clang_major__" exodus-wasm-runtime.json'
out=$($B rewrite --major $N --packages Packages exodus.json 2>&1); rc=$?
check "again: on LLVM $N already, nothing done" '[ $rc -eq 0 ] && echo "$out" | grep -q "already"'

for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
idx Packages.short libclang-cpp$N
out=$($B rewrite --major $N --packages Packages.short exodus.json 2>&1); rc=$?
check "a toolchain package missing from the index is an error, named" '[ $rc -ne 0 ] && echo "$out" | grep -q "libclang-cpp$N"'

for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
out=$($B rewrite --major $N --packages Packages exodus2.json 2>&1); rc=$?
check "one configuration alone that points into a package only another pins: named" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus2.json: still points into an LLVM $OLD package"'
for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
$B rewrite --major $N --packages Packages $CFGS > /dev/null 2>&1
LIBD="cache/apt/llvm-$N-dev/amd64/$NEWV/payload/usr/lib/llvm-$N/lib"; mkdir -p "$LIBD"
touch "$LIBD/libLLVM.so"
out=$($B libname --major $N --cache cache $CFGS 2>&1); rc=$?
check "libname: only libLLVM.so → -lLLVM" '[ $rc -eq 0 ] && grep -q "\"LLVM\"" exodus2.json && ! grep -q "\"LLVM-$N\"" exodus2.json'
touch "$LIBD/libLLVM-$N.so"
out=$($B libname --major $N --cache cache $CFGS 2>&1); rc=$?
check "libname: libLLVM-$N.so → -lLLVM-$N" '[ $rc -eq 0 ] && grep -q "\"LLVM-$N\"" exodus2.json'
rm "$LIBD"/libLLVM*.so
out=$($B libname --major $N --cache cache $CFGS 2>&1); rc=$?
check "libname: neither → an error naming the directory" '[ $rc -ne 0 ] && echo "$out" | grep -q "llvm-$N/lib"'
cd "$ROOT"

[ $fail -eq 0 ] && echo "llvm_bump: PASS" || echo "llvm_bump: FAIL"
exit $fail
