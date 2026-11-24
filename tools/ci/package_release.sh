#!/usr/bin/env bash
# Package the built compiler and runtime as a relocatable tarball:
#
#   aiml-<version>-linux-x86_64/
#     bin/aiml
#     lib/libaiml.so
#     lib/<shared libraries the two resolve from __exodus_cache/>
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

# Belongs to the target system (driver stack, display, audio, libc).
SKIP='^(ld-linux|libc\.|libm\.|libmvec\.|libdl\.|libpthread\.|librt\.|libresolv\.|libgcc_s|libstdc\+\+|libGL\.|libGLX|libEGL|libGLdispatch|libOpenGL|libgallium|libdrm|libgbm|libvulkan|libwayland|libX|libxcb|libxshmfence|libasound|libpulse|libjack|libudev|libsystemd)'

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

tar -C "$(dirname "$STAGE")" -czf "$OUT/$NAME.tar.gz" "$NAME"
(cd "$OUT" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
ls -l "$OUT"
