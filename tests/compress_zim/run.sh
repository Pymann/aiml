#!/usr/bin/env bash
# run.sh — ZIM archive tests (spec/33_2 §6).
#
# The archive is built here with the libzim WRITER, because AIML reads ZIM files and does not
# write them (§6), so there is no aiml way to produce the fixture.  The generator is compiled
# against the same cached libzim the compiler links, and the .zim is a build artefact under
# tests/data/ — not something to keep in the repository.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root

AIML="${AIML:-out/aiml/aiml}"
ZIM_DEV="__exodus_cache/apt/libzim-dev/amd64/8.2.1-2build1/payload"
ZIM_RT="__exodus_cache/apt/libzim8/amd64/8.2.1-2build1/payload/usr/lib/x86_64-linux-gnu"
XAPIAN_RT="__exodus_cache/apt/libxapian30/amd64/1.4.22-1build1/payload/usr/lib/x86_64-linux-gnu"
TOOL="out/zimtool/make_test_zim"
ARCHIVE="tests/data/aiml_zim_test.zim"

echo "── ZIM archives (spec/33_2 §6) ──"

if [ ! -d "$ZIM_DEV/usr/include/zim" ]; then
    echo "  SKIP  libzim headers not in the exodus cache — run: exodus pkg install"
    exit 0
fi

mkdir -p out/zimtool tests/data

# Always rebuild the generator.  It takes a second, and a stale one left over from a different
# set of link flags fails at load time ("libxapian.so.30: cannot open shared object file") in a
# way that looks like a test failure.
echo "  building the ZIM writer tool"
if true; then
    if ! g++ -std=c++17 -O1 -I"$ZIM_DEV/usr/include" \
            tests/compress_zim/make_test_zim.cpp -o "$TOOL" \
            -L"$ZIM_DEV/usr/lib/x86_64-linux-gnu" -L"$ZIM_RT" -L"$XAPIAN_RT" -lzim \
            -Wl,--disable-new-dtags \
            -Wl,-rpath,"$PWD/$ZIM_RT" -Wl,-rpath,"$PWD/$XAPIAN_RT" \
            -Wl,-rpath-link,"$PWD/$XAPIAN_RT"; then
        echo "  FAIL  could not build make_test_zim"
        exit 1
    fi
fi

# Always rebuild the archive: the test asserts on exactly these contents.
rm -f "$ARCHIVE"
if ! "$TOOL" "$ARCHIVE" > /dev/null; then
    echo "  FAIL  could not write $ARCHIVE"
    exit 1
fi
echo "  fixture $ARCHIVE ($(stat -c%s "$ARCHIVE") bytes)"

# ── 1. JIT: the forwarders compiled into the compiler binary ──
out=$(AIML_MEM_V3=1 "$AIML" tests/compress_zim/test_compress_zim.aiml -r 2>&1); rc=$?
echo "$out" | grep -E "PASS|FAIL|Exception|error" | head -20
if [ $rc -ne 0 ]; then
    echo "── compress_zim JIT FAILED (rc=$rc) ──"
    echo "$out" | tail -30
    exit 1
fi

# ── 2. AOT: the forwarders in libaiml.so, reached by a program that links only -laiml ──
# A DIFFERENT copy of the forwarders than the JIT above uses, and the one every deployed
# program takes.  Worth its own leg: the first version of the module exported the same
# aiml_zim_* names as the forwarders, so the table resolved back to them and aiml_zim_open
# called itself until the stack ran out — a fault only this path or the JIT can show.
# It also proves what the module is FOR: this link mentions neither libzim nor libxapian.
LIBAIML="out/libaiml"
if [ ! -f "$LIBAIML/libaiml.so" ]; then
    echo "  SKIP  AOT leg: $LIBAIML/libaiml.so is not built"
    echo "── compress_zim PASS (JIT only) ──"
    exit 0
fi
mkdir -p out/zimtool
if ! AIML_MEM_V3=1 "$AIML" tests/compress_zim/aot_probe.aiml -o out/zimtool/zim_aot.o > /dev/null 2>&1; then
    echo "  FAIL  AOT compile"
    exit 1
fi
if ! g++ out/zimtool/zim_aot.o -o out/zimtool/zim_aot \
        -L"$LIBAIML" -laiml -lpthread -lssl -lcrypto -ldl \
        -Wl,-rpath,"$PWD/$LIBAIML" 2>&1; then
    echo "  FAIL  AOT link"
    exit 1
fi
out=$(AIML_MEM_V3=1 ./out/zimtool/zim_aot 2>&1); rc=$?
echo "$out" | grep -E "PASS|FAIL|Exception|error" | head -20
if [ $rc -ne 0 ]; then
    echo "── compress_zim AOT FAILED (rc=$rc) ──"
    echo "$out" | tail -30
    exit 1
fi
echo "── compress_zim PASS (JIT + AOT) ──"
exit 0
