# 68 — aiml environments (venv)

An **aiml environment** ("venv") is one directory that carries a compiler, its runtime library
and installed packages, so that nothing else has to know where aiml was built.  Projects name
`aiml` and `libaiml`, never a path into somebody's build tree.  The same directory is what a
cosmos pipeline stage leaves behind as a layer (cosmos doc/spec/110 F110-112) and what ufo checks
out again (`ufo layer apply`), so a venv is built once and then only moved around.

## 1. Layout

```
<venv>/
  bin/aiml                       the compiler (JIT runner and object compiler)
  bin/<tool>                     installed tools: cosmos · kontor · ufo (§6)
  lib/libaiml.so                 the runtime library AOT programs link against
  lib/aiml/aiml.packages.json    the venv's package table (spec/21 §3.1)
  lib/aiml/aiml.installed.json   what `install` placed, and where it came from (§6; only
                                 once something is installed)
  lib/aiml/<package>/…           installed packages, one directory each
  env                            the environment, for `. <venv>/env`
```

- **F68-1** `bin/aiml` has `RUNPATH $ORIGIN/../lib`, `lib/libaiml.so` has `RUNPATH $ORIGIN`.
  A library placed into `lib/` (the load chain of cosmos doc/spec/94 §5) is found from both
  without `LD_LIBRARY_PATH` and without `ldconfig`.  The system directories the build adds stay
  behind `$ORIGIN`, so a library in the venv wins over a system copy.
- **F68-2** A venv holds **no cache**.  The object cache stays where `AIML_CACHE` points, else
  `./__aiml_cache` (spec/29_2 §12).  A layer then contains exactly what was installed, and two
  builds of the same sources give the same layer.

## 2. Creating and updating: `aiml venv <dir> [--runtime <libaiml.so>]`

- **F68-10** Creates `<dir>` (and its parents) with the layout of §1: copies the running
  compiler to `bin/aiml` and the runtime to `lib/libaiml.so`, creates `lib/aiml/` with an empty
  table `{"packages": {}}` if none exists, and writes `env`.  `<dir>` is made absolute first.
- **F68-11** The runtime is `--runtime <path>`, else the first that exists of
  `<exe>/../lib/libaiml.so` (running from a venv) and `<exe>/../libaiml/libaiml.so` (running from
  the build tree, `out/aiml/aiml` next to `out/libaiml/libaiml.so`).  `<exe>` is the directory
  of the running binary.  None found → exit 1, and the message names `--runtime`.
- **F68-12** Every file is written to `<name>.tmp.<pid>` and renamed into place, so a compiler
  that is running from the venv while it is updated keeps its old image.  A source that **is**
  the target (`<venv>/bin/aiml venv <venv>`) is left alone.
- **F68-13** `<dir>` must be missing, empty or a venv already (it has an `env` whose first line
  is the venv marker of §3).  Anything else → exit 1, nothing written — `aiml venv /usr` must not
  scatter files into a system directory.
- **F68-14** Running it again is the update: new compiler and runtime, the same packages, the
  table kept.  Running it from a moved venv for its new place (`<new>/bin/aiml venv <new>`) only
  rewrites `env`; the binaries are relocatable by F68-1.

## 3. `env`

```sh
# aiml venv — written by `aiml venv`; use with `. <venv>/env`
export AIML_VENV='/opt/aiml'
export AIML='/opt/aiml/bin/aiml'
export PATH='/opt/aiml/bin'"${PATH:+:$PATH}"
```

- **F68-20** The first line is the marker F68-13 checks.  The file is POSIX `sh`, and it sets
  only these three variables.  A path is single-quoted; a path with a single quote is refused
  (exit 1), so the file cannot be made to run anything.
- **F68-21** `AIML` names the compiler.  A tool that runs the compiler (exodus, test drivers)
  takes `$AIML`, else `aiml` from `PATH` — never a path of its own.
- **F68-22** A program built against the venv links `-L$AIML_VENV/lib -laiml`.  If it is
  installed into `<venv>/bin`, it gets `RUNPATH $ORIGIN/../lib`; elsewhere it gets
  `RUNPATH $AIML_VENV/lib`.  `env` sets neither `LIBRARY_PATH` nor `LD_LIBRARY_PATH`: what a
  binary needs is in the binary, so it runs without the venv's environment too.

## 4. Packages of a venv

- **F68-30** The venv's table is the **last** in the lookup order of spec/21 §3.1: after a
  host-set table, `$AIML_PACKAGES`, and `aiml.packages.json` in the working directory or upward
  from the importing file comes `<venv>/lib/aiml/aiml.packages.json` of the venv the compiler
  belongs to.  A project's own table always wins.
- **F68-31** "The venv the compiler belongs to": the file that contains the compiler (the
  `aiml` binary, or `libaiml.so` in a host such as the cosmos agent) is `<root>/bin/aiml` or
  `<root>/lib/libaiml.so`, and `<root>/env` carries the marker.  It is found from the image
  itself (`dladdr`), not from `AIML_VENV`, so a compiler always reads its own venv's packages
  and not those of the environment it was started from.
- **F68-32** A relative `path` in the venv's table is relative to `lib/aiml/` (spec/21: to the
  table's directory), so a moved venv keeps its packages.

## 5. As a layer

- **F68-40** A cosmos stage that creates the venv in the root (`aiml venv /opt/aiml`) is the
  stage whose upper directory becomes the venv layer; `push:` cuts after it.  Nothing outside
  `/opt/aiml` is needed: no symlink to a host path, no `/etc/ld.so.conf.d` entry.
- **F68-41** `ufo layer apply <dir> <layer>` gives the venv back under `<dir>/opt/aiml`;
  `. <dir>/opt/aiml/env` is enough to use it there.  Moved there, F68-14 rewrites `env`.

Tests: `tests/venv/run.sh` (layout, RUNPATHs, `env` and `AIML`/`PATH`, a JIT run from `PATH`,
an AOT program in `bin/` against the venv's runtime, the venv's package table and its precedence,
refusing a foreign directory, update from inside the venv, a moved venv).

## 6. Installing tools — `aiml venv <dir> install <name> <source>`

§1–§5 give a venv the compiler, the runtime and importable packages.  §6 adds the **tools** that
are written in aiml and built with it — cosmos, kontor, ufo — so that a machine with a venv has
them without anybody cloning a repository by hand.

```
aiml venv <dir> install <name> --git <url> [--ref <tag|branch|commit>]
aiml venv <dir> install <name> --path <dir>
aiml venv <dir> install <name> --kontor <url> [--version <v>]      (§6.1, not yet)
aiml venv <dir> list
aiml venv <dir> remove <name>
```

- **F68-50** **Sources.**  `--git` is the normal one: the ref is resolved to a commit with
  `git ls-remote <url> <ref>` **before** the clone, so what was built is written down (`HEAD` is
  the default).  `--path <dir>` installs a local tree — for a tool that has no repository yet, and
  for testing; its origin is recorded as `path:<absolute>` and `list` marks it **unreproducible**,
  because nobody can say later what was in that directory.
- **F68-51** The fetcher is `git` from `PATH`.  Missing → exit 1, and the message names it.  ufo
  is the git client of this ecosystem, but a venv must be able to install **ufo itself**, so the
  first fetch cannot depend on it; `--vcs ufo` (later) chooses the venv's own `bin/ufo` instead.
- **F68-52** **Nothing is fetched or built inside the venv** (F68-2): the clone and the build live
  under `$AIML_CACHE/venv_tools/<name>/<commit>/`, else `./__aiml_cache/venv_tools/…`.  A
  directory that already holds that commit is reused, so a second install does not clone again.
  The venv receives **results only** — it stays what §5 can cut as a layer.
- **F68-53** **The build is the tool's own.**  In the fetched tree, `install` runs
  `$EXODUS build --all` (else `exodus` from `PATH`, F68-21), with `AIML_VENV`, `AIML` and `PATH`
  pointing at the **target venv**: a tool is built by the compiler it will live next to, and links
  against that venv's runtime (F68-22).  A failed build installs nothing (exit 1, the build's
  output is passed through).
  A tool whose build configuration names a **build tree** instead (ufo's `exodus.json` has
  `"name": "../aiml/out/aiml/aiml"` and `-L../aiml/out/libaiml`) is refused with exactly that
  reason: it would be built by a foreign compiler and link against a library that is not in the
  venv.  Taking the compiler from `$AIML` and the runtime from `$AIML_VENV/lib` (F68-21, F68-22)
  is therefore the entry ticket for a tool, not a detail of its project.
- **F68-54** **The repository says what to install**, in `aiml.tool.json` at its root:

  ```json
  { "name": "ufo", "version": "0.2.0",
    "programs":  ["out/ufo/ufo", "out/ufo-layer/ufo-layer"],
    "libraries": ["out/libufo/libufo.so"],
    "package":   "lib" }
  ```

  `programs` go to `<venv>/bin/`, `libraries` to `<venv>/lib/`, and `package` names the directory
  of the source tree that becomes the importable package `lib/aiml/<name>/` (spec/21 §3.1), copied
  in and entered in the table with a path relative to `lib/aiml/` (F68-32).  Every key is optional
  except `name`; a tool may be only programs (cosmos) or only a package.  **No file → exit 1**
  („the repository has no aiml.tool.json"), and nothing is guessed: a venv that installed by
  guessing would not be the same venv as the next one.  The `name` must match the install's
  `<name>`.
- **F68-55** **The copies are relocatable.**  A program placed in `bin/` gets
  `RUNPATH $ORIGIN/../lib`, a library in `lib/` gets `RUNPATH $ORIGIN` — as F68-1 does for the
  compiler, and for the same reason: `. <venv>/env` is for people, a binary must run without it.
  Paths of the build host that the build left behind stay **after** `$ORIGIN`.
- **F68-56** **The record.**  `lib/aiml/aiml.installed.json` holds, per name: `source`
  (`git:<url>` / `path:<dir>` / `kontor:<url>`), `commit`, `version`, and the list of files placed
  (relative to the venv).  `list` prints it; `remove <name>` deletes exactly those files, its
  package directory and its table entry — and nothing else.  Written with the rename of F68-12.
  The file **comes into being with the first install**: a venv that installed nothing has the
  layout of §1 unchanged, down to the file count — ufo's `tests/venv_layer.sh` checks a venv layer
  against exactly four paths, and §6 must not add a fifth to every venv.
- **F68-57** **Installing again is the update.**  The same name at the same commit with the same
  file list is a no-op (it says so and exits 0).  A different commit replaces: the old files are
  removed first, so a renamed program leaves nothing behind.
- **F68-58** `install` **does not touch the system.**  No apt, no `/etc`, no `sudo`.  A tool that
  needs system libraries (cosmos needs libldap and xmlsec1) says so in its own project; the venv
  layer sits on a base image that has them (cosmos doc/spec/111 §111.10).
- **F68-59** The installed tools travel **with the venv**: `push:` of the venv stage (F68-40) and
  `ufo layer apply` (F68-41) carry `bin/`, `lib/` and the two tables, so a venv layer is a
  toolbox, not just a compiler.

### 6.1 kontor as a source (planned)

kontor keeps `/exodus/<name>/<version>/` (exodus.json plus the package) and is where these tools
are published (cosmos doc/spec/111 §111.9).  `--kontor <url> --version <v>` fetches that instead
of cloning, which makes an install reproducible without a git server and without a build when
kontor carries binaries.  Until then `--git` is the way, and `--kontor` exits 1 naming §6.1.

Tests: `tests/venv/run.sh` (install from a local path and from a git repository made on the fly;
the manifest's three kinds; RUNPATHs of the copies; the record, `list` and `remove`; installing
the same commit twice; a different commit replacing; a missing manifest, a wrong `name`, a failing
build, a missing `git`; nothing fetched into the venv).
