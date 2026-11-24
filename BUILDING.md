# Building AIML

This document describes the build exactly as it is performed today, including
the parts that are not yet mechanised. Where a step has not been verified on a
clean machine, it says so.

---

## 1. What you need

| Requirement | Version used | Note |
| --- | --- | --- |
| OS | Ubuntu 24.04 LTS, x86-64 | the only host the build is verified on |
| C++ compiler | `g++` (C++17) | `compiler.flags` in `exodus.json` |
| Python | 3.12 | for the build tool |
| `apt-get`, `dpkg-deb` | from the base system | the build tool downloads and unpacks `.deb` payloads with them |
| Exodus | current `master` | the build tool, see below |
| System headers | `libx11-dev`, `libxi-dev`, `liblzma-dev`, `libsharpyuv-dev`, `linux-libc-dev` | the only headers not yet pinned; install with apt |
| System libraries | 162 pinned apt packages | fetched by Exodus into `__exodus_cache/`, **no root required** |

The dependency set is large: LLVM 18, yaml-cpp, OpenSSL, zlib/brotli/zstd/lz4/xz,
libzip, Assimp, OpenGL/GLEW/GLFW, libpng, libwebp, OpenBLAS, X11/Xtst, libssh,
OpenLDAP, GnuTLS, SASL, xmlsec1, libxml2/libxslt. Every entry is pinned by
version **and** sha256 digest in the `apt_packages` array of `exodus.json`; see
[THIRD_PARTY.md](THIRD_PARTY.md) for the licenses.

macOS and Windows hosts are untested. `wasm32` is a supported *target*, not a
supported host (section 6).

## 2. Install the build tool

Exodus is **not** the `exodus` package on PyPI — that name belongs to an
unrelated project. Install it from git:

```bash
python3 -m venv ~/exvenv
source ~/exvenv/bin/activate
pip install "git+https://github.com/Pymann/exodus.git"
exodus --version
```

## 3. Fetch the pinned dependencies

From the repository root:

```bash
exodus pkg install          # the 162 apt packages of exodus.json
exodus pkg install --all    # additionally the git packages of the other configs
                            # (emsdk 3.1.74 and yaml-cpp 0.8.0 — only needed for
                            # the wasm32 target; a large download)
```

This downloads each configured `.deb` with `apt-get download`, verifies its
digest, and unpacks it into `__exodus_cache/apt/<package>/<arch>/<version>/payload/`.
Nothing is installed system-wide and no `sudo` is needed. The include and library
search paths in `exodus.json` point into that cache, so the build is pinned to
those exact payloads.

Run `apt-get update` first if the package lists are stale, otherwise
`apt-get download` cannot resolve the pinned versions.

**Caveat:** Ubuntu mirrors drop superseded point releases. If a pinned version is
no longer on the mirror, either take it from `https://snapshot.ubuntu.com/` or
re-pin it with `exodus pkg add`. This is the weakest link in the reproducibility
chain today.

## 4. Build

On a **fresh clone**, build the compiler and the runtime first, because the
other configs need them:

```bash
exodus build -c exodus.json     # the compiler  → out/aiml/aiml
exodus build -c exodus2.json    # the runtime   → out/libaiml/libaiml.so
```

Afterwards, and for every later change:

```bash
exodus build --all
```

Note that `-c` takes **one** config: passing it twice silently keeps only the
last one. `--all` builds every `exodus*.json` in the repository root that declares the
Exodus project schema. **Always use `--all`** — building a single config leaves
the other artifacts stale against the compiler you just changed. The relevant
configs are:

| Config | Artifact | What it is |
| --- | --- | --- |
| `exodus.json` | `out/aiml/aiml` | the compiler/JIT driver (`output_type: executable`) |
| `exodus2.json` | `out/libaiml/libaiml.so` | the runtime library AOT output links against |
| `exodus-wasm-runtime.json` | `out/aiml_wasm/` | the wasm32 runtime |
| `exodus-test.json` | `out-test/` | the C++ unit tests |
| `exodus-asan.json` | `out-asan/` | AddressSanitizer build for the leak gates |
| `exodus-zim.json` | `out/libaiml_zim/` | optional ZIM module — **GPL-2.0-or-later**, see [THIRD_PARTY.md](THIRD_PARTY.md) |

`--all` orders the configs by their declared `dependencies` and keeps going
after a failure, reporting one errorlevel per config. Two consequences:

* `exodus-wasm-runtime.json` fails unless emsdk was installed
  (`exodus pkg install --all`). On a host without it, that one red entry in the
  `--all` summary is expected.
* `exodus-aot-repro.json` and `exodus-test-link-bug.json` are AOT repro
  harnesses: they use the freshly built `out/aiml/aiml` as their compiler and
  depend on `aiml` and `libaiml`.

Incremental rebuilds are the normal case; Exodus invalidates objects against
source and compiler mtimes itself. Do not delete `__aiml_cache/` by hand — that
only forces an unnecessary cold rebuild.

## 5. Run a program, run the tests

```bash
out/aiml/aiml tests/test.aiml          # JIT-compile and run; exit code is the program's
out/aiml/aiml prog.aiml -o prog.o      # AOT: emit an object file (+ prog.o.deps)
./run_all_tests.sh                     # the whole suite
./run_all_tests.sh --only lang         # only matching tests
./run_all_tests.sh --no-build          # skip the build step
./run_aot_tests.sh                     # AOT smoke tests: compile, link, run
```

Linking an AOT object by hand (what `run_aot_tests.sh` does):

```bash
g++ -o prog prog.o $(cat prog.o.deps) \
    -Lout/libaiml -laiml -lpthread -ldl -lm -lstdc++ \
    -Wl,-rpath,out/libaiml
```

Expectations for a clean run, measured on 2026-11-07
(1052 entries: 866 passed, 8 failed, 178 skipped):

* **7 of the 8 failures are expected.** They are acceptance targets written
  before the implementation (spec first): `spec/62` pipelines and `spec/64`
  closures are `[PROPOSAL]`, and `sys.bundle` (`spec/29_1`) has no handler yet.
  They are supposed to be red until those chapters are implemented.
* The 8th, `tests/venv/kontor_stub.aiml`, exceeds the 600 s per-test limit and is
  a genuine open failure.
* Of the 178 skips, 103 are files executed by their own `run.sh` driver (the
  driver is counted instead), 35 need an interactive display, 4 need a network
  peer, 2 need a wasm toolchain, 1 is manual-only. Display tests also need a TTY;
  `script -qec` with `DISPLAY=:99` (Xvfb) runs them.

There is **no CI yet**, and the suite does not distinguish expected failures from
regressions — that separation is a prerequisite for a public CI.

## 6. Other targets

* **wasm32** — `exodus build -c exodus-wasm-runtime.json` plus `run_wasm_tests.sh`.
  Needs an `emsdk` installation and node; see `doc/wasm_frame_loop.md`.
* **Sanitizers / leak gates** — `exodus-asan.json` for ASan; the valgrind gates
  live under `tests/lifetime/` and `tests/_mem_sanitize/`. Never run a sanitizer
  gate and the normal suite at the same time; the measurements interfere.

## 7. Container build

[`Dockerfile`](Dockerfile) encodes sections 1–4 for `ubuntu:24.04`.

**This image has not been built end to end yet** — no container runtime was
available on the development machine. It is the host recipe transcribed, not a
verified artifact. Treat a successful `docker build` as the point at which
section 1–4 becomes genuinely reproducible for third parties, and until then
assume the host path is the only verified one.

```bash
docker build -t aiml:dev .
docker run --rm aiml:dev out/aiml/aiml tests/test.aiml
```

The image builds the compiler only. Graphics, audio, display and network tests
need devices and peers a plain container does not have.
