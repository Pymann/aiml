# 69 — aiml environments (venv)

An **aiml environment** ("venv") is one directory that carries a compiler, its runtime library
and installed packages, so that nothing else has to know where aiml was built.  Projects name
`aiml` and `libaiml`, never a path into somebody's build tree.  The same directory is what a
cosmos pipeline stage leaves behind as a layer (cosmos doc/spec/110 F110-112) and what ufo checks
out again (`ufo layer apply`), so a venv is built once and then only moved around.

## 1. Layout

```
<venv>/
  bin/aiml                       the compiler (JIT runner and object compiler)
  lib/libaiml.so                 the runtime library AOT programs link against
  lib/aiml/aiml.packages.json    the venv's package table (spec/21 §3.1)
  lib/aiml/<package>/…           installed packages, one directory each
  env                            the environment, for `. <venv>/env`
```

- **F69-1** `bin/aiml` has `RUNPATH $ORIGIN/../lib`, `lib/libaiml.so` has `RUNPATH $ORIGIN`.
  A library placed into `lib/` (the load chain of cosmos doc/spec/94 §5) is found from both
  without `LD_LIBRARY_PATH` and without `ldconfig`.  The system directories the build adds stay
  behind `$ORIGIN`, so a library in the venv wins over a system copy.
- **F69-2** A venv holds **no cache**.  The object cache stays where `AIML_CACHE` points, else
  `./__aiml_cache` (spec/29_2 §12).  A layer then contains exactly what was installed, and two
  builds of the same sources give the same layer.

## 2. Creating and updating: `aiml venv <dir> [--runtime <libaiml.so>]`

- **F69-10** Creates `<dir>` (and its parents) with the layout of §1: copies the running
  compiler to `bin/aiml` and the runtime to `lib/libaiml.so`, creates `lib/aiml/` with an empty
  table `{"packages": {}}` if none exists, and writes `env`.  `<dir>` is made absolute first.
- **F69-11** The runtime is `--runtime <path>`, else the first that exists of
  `<exe>/../lib/libaiml.so` (running from a venv) and `<exe>/../libaiml/libaiml.so` (running from
  the build tree, `out/aiml/aiml` next to `out/libaiml/libaiml.so`).  `<exe>` is the directory
  of the running binary.  None found → exit 1, and the message names `--runtime`.
- **F69-12** Every file is written to `<name>.tmp.<pid>` and renamed into place, so a compiler
  that is running from the venv while it is updated keeps its old image.  A source that **is**
  the target (`<venv>/bin/aiml venv <venv>`) is left alone.
- **F69-13** `<dir>` must be missing, empty or a venv already (it has an `env` whose first line
  is the venv marker of §3).  Anything else → exit 1, nothing written — `aiml venv /usr` must not
  scatter files into a system directory.
- **F69-14** Running it again is the update: new compiler and runtime, the same packages, the
  table kept.  Running it from a moved venv for its new place (`<new>/bin/aiml venv <new>`) only
  rewrites `env`; the binaries are relocatable by F69-1.

## 3. `env`

```sh
# aiml venv — written by `aiml venv`; use with `. <venv>/env`
export AIML_VENV='/opt/aiml'
export AIML='/opt/aiml/bin/aiml'
export PATH='/opt/aiml/bin'"${PATH:+:$PATH}"
```

- **F69-20** The first line is the marker F69-13 checks.  The file is POSIX `sh`, and it sets
  only these three variables.  A path is single-quoted; a path with a single quote is refused
  (exit 1), so the file cannot be made to run anything.
- **F69-21** `AIML` names the compiler.  A tool that runs the compiler (exodus, test drivers)
  takes `$AIML`, else `aiml` from `PATH` — never a path of its own.
- **F69-22** A program built against the venv links `-L$AIML_VENV/lib -laiml`.  If it is
  installed into `<venv>/bin`, it gets `RUNPATH $ORIGIN/../lib`; elsewhere it gets
  `RUNPATH $AIML_VENV/lib`.  `env` sets neither `LIBRARY_PATH` nor `LD_LIBRARY_PATH`: what a
  binary needs is in the binary, so it runs without the venv's environment too.

## 4. Packages of a venv

- **F69-30** The venv's table is the **last** in the lookup order of spec/21 §3.1: after a
  host-set table, `$AIML_PACKAGES`, and `aiml.packages.json` in the working directory or upward
  from the importing file comes `<venv>/lib/aiml/aiml.packages.json` of the venv the compiler
  belongs to.  A project's own table always wins.
- **F69-31** "The venv the compiler belongs to": the file that contains the compiler (the
  `aiml` binary, or `libaiml.so` in a host such as the cosmos agent) is `<root>/bin/aiml` or
  `<root>/lib/libaiml.so`, and `<root>/env` carries the marker.  It is found from the image
  itself (`dladdr`), not from `AIML_VENV`, so a compiler always reads its own venv's packages
  and not those of the environment it was started from.
- **F69-32** A relative `path` in the venv's table is relative to `lib/aiml/` (spec/21: to the
  table's directory), so a moved venv keeps its packages.

## 5. As a layer

- **F69-40** A cosmos stage that creates the venv in the root (`aiml venv /opt/aiml`) is the
  stage whose upper directory becomes the venv layer; `push:` cuts after it.  Nothing outside
  `/opt/aiml` is needed: no symlink to a host path, no `/etc/ld.so.conf.d` entry.
- **F69-41** `ufo layer apply <dir> <layer>` gives the venv back under `<dir>/opt/aiml`;
  `. <dir>/opt/aiml/env` is enough to use it there.  Moved there, F69-14 rewrites `env`.

Tests: `tests/venv/run.sh` (layout, RUNPATHs, `env` and `AIML`/`PATH`, a JIT run from `PATH`,
an AOT program in `bin/` against the venv's runtime, the venv's package table and its precedence,
refusing a foreign directory, update from inside the venv, a moved venv).
