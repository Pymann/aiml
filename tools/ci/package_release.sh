#!/usr/bin/env bash
# Package the built compiler and runtime as a relocatable tarball:
#
#   aiml-<version>-linux-x86_64/
#     bin/aiml
#     lib/libaiml.so
#     lib/<shared libraries the two resolve from __exodus_cache/>
#     lib/wasm32/libaiml_wasm.a, lib/wasm32/emsdk.version   (spec/68 §8)
#
# The pinned libraries come along; libc, the GL/X11/audio stack and other
# libraries that belong to the target system do not. Every bundled library
# gets RUNPATH $ORIGIN so its own dependencies are found next to it.
#
# Usage: tools/ci/package_release.sh <version> [outdir]
set -euo pipefail

VERSION="${1:?version}"
OUT="${2:-dist}"
NAME="aiml-${VERSION}-linux-x86_64"
STAGE="$(mktemp -d)/${NAME}"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$OUT"

cp out/aiml/aiml "$STAGE/bin/aiml"
cp out/libaiml/libaiml.so "$STAGE/lib/libaiml.so"
cp LICENSE THIRD_PARTY.md "$STAGE/"

# Belongs to the target system, for one of two reasons.
#
# MUST come from the host: glibc and its loader (a bundled libc beside the host's
# loader is the classic unexplainable crash), and the driver stack -- libGL,
# libGLX, libEGL, libgallium, libdrm, libgbm, libvulkan have to match the kernel
# driver on the machine that runs this.
#
# libstdc++ / libgcc_s are skipped for a subtler reason: the host's libGL (Mesa)
# links libstdc++ too, and because every bundled library carries RUNPATH $ORIGIN
# the bundled copy wins process-wide. On a host newer than the build runner that
# turns a working driver into an unresolved symbol -- so these two stay host even
# though bundling them would be only 2.7 MiB.
#
# The X11/audio CLIENT libraries are bundled (since 2026-11-25): libX11, libxcb,
# libXext, libXtst, libXau, libXdmcp and friends are ~1.6 MiB together, have a
# stable ABI and no coupling to the driver, and without them the bundle needs the
# X client libraries preinstalled on the target.
SKIP='^(ld-linux|libc\.|libm\.|libmvec\.|libdl\.|libpthread\.|librt\.|libresolv\.|libgcc_s|libstdc\+\+|libGL\.|libGLX|libEGL|libGLdispatch|libOpenGL|libgallium|libdrm|libgbm|libvulkan|libudev|libsystemd)'

# Index the pinned shared libraries by file name.  A NEEDED entry is bundled
# when the cache has it — whether or not the build host also has a system copy
# (CI runners ship LLVM, zlib, ... system-wide; a user's machine need not).
declare -A pinned=()
while IFS= read -r p; do
    n="${p##*/}"; [ -n "${pinned[$n]:-}" ] || pinned[$n]="$p"
done < <(find "${EXODUS_CACHE:-__exodus_cache}/apt" -path '*/payload/*' \( -type f -o -type l \) -name 'lib*.so*' 2>/dev/null | sort)

declare -A seen=()
queue=("$STAGE/bin/aiml" "$STAGE/lib/libaiml.so")
while [ "${#queue[@]}" -gt 0 ]; do
    f="${queue[0]}"; queue=("${queue[@]:1}")
    while read -r lib; do
        [[ "$lib" =~ $SKIP ]] && continue
        [ -n "${seen[$lib]:-}" ] && continue
        src="${pinned[$lib]:-}"
        [ -n "$src" ] && [ -e "$src" ] || continue   # not pinned: the target system provides it
        seen[$lib]=1
        cp -L "$src" "$STAGE/lib/$lib"
        chmod u+w "$STAGE/lib/$lib"
        queue+=("$STAGE/lib/$lib")
    done < <(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
done

for f in "$STAGE"/lib/*.so*; do patchelf --set-rpath '$ORIGIN' "$f"; done
patchelf --set-rpath '$ORIGIN/../lib' "$STAGE/bin/aiml"

echo "bundled ${#seen[@]} libraries:"; ls "$STAGE/lib"

# Run the bundle away from the checkout, so neither the relative cache rpaths
# nor out/ can help it.
probe="$(mktemp -d)"
cp tests/test.aiml "$probe/"
missing="$(cd "$probe" && ldd "$STAGE/bin/aiml" | grep 'not found' || true)"
if [ -n "$missing" ]; then echo "unresolved libraries:"; echo "$missing"; exit 1; fi
set +e
(cd "$probe" && "$STAGE/bin/aiml" test.aiml)
rc=$?
set -e
echo "bundle smoke test exit code $rc"
[ "$rc" -eq 42 ]   # tests/test.aiml ends in sys.exit(42)

# spec/68 §8: the wasm32 runtime and the emsdk it was built with (F68-90/91),
# proved by linking a program against the packaged copy (F68-93).
# REQUIRE_WASM=1 (CI) makes a missing archive an error.
WASM_LIB="out/aiml_wasm/aiml_wasm/libaiml_wasm.a"
if [ -f "$WASM_LIB" ]; then
    EMSDK_VERSION="$(python3 - <<'PY'
import json
cfg = json.load(open("exodus-wasm-runtime.json"))
for pkg in cfg.get("git_packages", []):
    if pkg.get("name") == "emsdk":
        for cmd in pkg.get("setup_commands", []):
            if len(cmd) >= 3 and cmd[1] == "install":
                print(cmd[2])
PY
)"
    [ -n "$EMSDK_VERSION" ] || { echo "no emsdk install command in exodus-wasm-runtime.json"; exit 1; }
    mkdir -p "$STAGE/lib/wasm32"
    cp "$WASM_LIB" "$STAGE/lib/wasm32/libaiml_wasm.a"
    echo "$EMSDK_VERSION" > "$STAGE/lib/wasm32/emsdk.version"
    EMSDK_ROOT="${EXODUS_CACHE:-__exodus_cache}/git/emsdk/current"
    EMCC="$EMSDK_ROOT/upstream/emscripten/emcc"
    NODE="$(ls "$EMSDK_ROOT"/node/*/bin/node 2>/dev/null | head -1)"
    [ -x "$EMCC" ] && [ -n "$NODE" ] || { echo "emsdk not found at $EMSDK_ROOT"; exit 1; }
    "$EMCC" --version | head -1 | grep -qF "$EMSDK_VERSION" \
        || { echo "emcc is not emsdk $EMSDK_VERSION: $("$EMCC" --version | head -1)"; exit 1; }
    wprobe="$(mktemp -d)"
    printf -- '- call: [sys.exit, 42]\n' > "$wprobe/exit42.aiml"
    (cd "$wprobe" && "$STAGE/bin/aiml" exit42.aiml --target wasm32 --emit-main -o exit42.ll)
    "$EMCC" "$wprobe/exit42.ll" -o "$wprobe/exit42.js" -L"$STAGE/lib/wasm32" -laiml_wasm \
        -fexceptions -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=node
    set +e
    (cd "$wprobe" && "$NODE" exit42.js)
    wrc=$?
    set -e
    echo "wasm32 smoke test (emsdk $EMSDK_VERSION) exit code $wrc"
    [ "$wrc" -eq 42 ]
elif [ "${REQUIRE_WASM:-0}" = 1 ]; then
    echo "missing $WASM_LIB: build exodus-wasm-runtime.json first (spec/68 F68-90)"; exit 1
fi

tar -C "$(dirname "$STAGE")" -czf "$OUT/$NAME.tar.gz" "$NAME"
(cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
ls -l "$OUT"
