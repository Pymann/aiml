#!/usr/bin/env bash
# run.sh — tools/ci/llvm_bump.py, the configuration side of the LLVM update
# workflow (TEST_SUITES.md §4.2): on copies of the real exodus configurations and
# an apt Packages index written here.
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
NEWV='1:23.1.8~++20261020082717+0a1b2c3d4e5f-1~exp1~20261020082834.93'
idx() {  # idx <file> [skip-package] — an index with the toolchain at 23
    : > "$1"
    for p in llvm-23 llvm-23-dev llvm-23-runtime llvm-23-tools llvm-23-linker-tools libllvm23 libclang-cpp23; do
        [ "$p" = "${2:-}" ] && continue
        sum=$(printf '%s' "$p" | sha256sum | cut -d' ' -f1)
        printf 'Package: %s\nArchitecture: amd64\nVersion: 1:23.1.2-1~exp1\nSHA256: 0000\n\n' "$p" >> "$1"
        printf 'Package: %s\nArchitecture: amd64\nVersion: %s\nDepends: libc6 (>= 2.34),\n libstdc++6\nFilename: pool/main/l/llvm-toolchain-23/%s.deb\nSHA256: %s\n\n' "$p" "$NEWV" "$p" "$sum" >> "$1"
    done
}
idx Packages

echo "── llvm_bump.py (TEST_SUITES.md §4.2) ──"
check "current: the configurations are on LLVM 18" '[ "$($B current exodus.json)" = 18 ]'
cp exodus.json exodus.before
out=$($B rewrite --major 23 --packages Packages $CFGS 2>&1); rc=$?
check "rewrite moves every configuration" '[ $rc -eq 0 ] && [ "$(echo "$out" | grep -c "LLVM 18 → 23")" -eq 5 ]'
[ $rc -eq 0 ] || echo "$out" | tail -5
check "nothing of LLVM 18 is left" '! grep -l "llvm-18\|llvm-c-18\|libllvm18\|libclang-cpp18\|LLVM-18\|1:18\.1\.3" $CFGS'
check "the newest build in the index is taken, with its sha256" \
  'python3 -c "import json,sys;p={x[\"name\"]:x for x in json.load(open(\"exodus.json\"))[\"apt_packages\"]};d=p[\"llvm-23-dev\"];sys.exit(0 if d[\"version\"]==\"$NEWV\" and d[\"digest\"]==\"$(printf llvm-23-dev | sha256sum | cut -d" " -f1)\" else 1)"'
check "the include paths follow: package, version, llvm-23 and llvm-c-23" \
  'grep -qF "__exodus_cache/apt/llvm-23-dev/amd64/$NEWV/payload/usr/include/llvm-23\"" exodus.json && grep -qF "__exodus_cache/apt/llvm-23-dev/amd64/$NEWV/payload/usr/include/llvm-c-23\"" exodus.json'
check "the library paths follow" 'grep -qF "__exodus_cache/apt/libllvm23/amd64/$NEWV/payload/usr/lib/llvm-23/lib\"" exodus2.json'
check "the library LLVM-18 becomes LLVM-23" 'grep -q "\"LLVM-23\"" exodus2.json'
check "libllvm20 (Mesa's) is not touched" \
  'grep -qF "\"name\": \"libllvm20\"" exodus.json && grep -qF "apt/libllvm20/amd64/1:20.1.2-0ubuntu1~24.04.3/payload" exodus.json'
check "no package is lost or gained" \
  '[ "$(python3 -c "import json;print(len(json.load(open(\"exodus.json\"))[\"apt_packages\"]))")" = "$(python3 -c "import json;print(len(json.load(open(\"exodus.before\"))[\"apt_packages\"]))")" ]'
check "everything else is as it was (the diff is the bump)" \
  '[ -z "$(diff <(grep -v -i "llvm\|clang-cpp\|\"digest\"\|\"version\": \"1:" exodus.before) <(grep -v -i "llvm\|clang-cpp\|\"digest\"\|\"version\": \"1:" exodus.json))" ]'
check "the clang macros of the wasm runtime stay" 'grep -q "__clang_major__" exodus-wasm-runtime.json'
out=$($B rewrite --major 23 --packages Packages exodus.json 2>&1); rc=$?
check "again: on LLVM 23 already, nothing done" '[ $rc -eq 0 ] && echo "$out" | grep -q "already"'

for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
idx Packages.short libclang-cpp23
out=$($B rewrite --major 23 --packages Packages.short exodus.json 2>&1); rc=$?
check "a toolchain package missing from the index is an error, named" '[ $rc -ne 0 ] && echo "$out" | grep -q "libclang-cpp23"'

for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
out=$($B rewrite --major 23 --packages Packages exodus2.json 2>&1); rc=$?
check "one configuration alone that points into a package only another pins: named" \
  '[ $rc -ne 0 ] && echo "$out" | grep -q "exodus2.json: still points into an LLVM 18 package"'
for f in $CFGS; do cp "$ROOT/$f" "$WORK/"; done
$B rewrite --major 23 --packages Packages $CFGS > /dev/null 2>&1
LIBD="cache/apt/llvm-23-dev/amd64/$NEWV/payload/usr/lib/llvm-23/lib"; mkdir -p "$LIBD"
touch "$LIBD/libLLVM.so"
out=$($B libname --major 23 --cache cache $CFGS 2>&1); rc=$?
check "libname: only libLLVM.so → -lLLVM" '[ $rc -eq 0 ] && grep -q "\"LLVM\"" exodus2.json && ! grep -q "\"LLVM-23\"" exodus2.json'
touch "$LIBD/libLLVM-23.so"
out=$($B libname --major 23 --cache cache $CFGS 2>&1); rc=$?
check "libname: libLLVM-23.so → -lLLVM-23" '[ $rc -eq 0 ] && grep -q "\"LLVM-23\"" exodus2.json'
rm "$LIBD"/libLLVM*.so
out=$($B libname --major 23 --cache cache $CFGS 2>&1); rc=$?
check "libname: neither → an error naming the directory" '[ $rc -ne 0 ] && echo "$out" | grep -q "llvm-23/lib"'
cd "$ROOT"

[ $fail -eq 0 ] && echo "llvm_bump: PASS" || echo "llvm_bump: FAIL"
exit $fail
