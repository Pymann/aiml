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
  takes `$AIML`, then `$AIML_VENV/bin/aiml` when a venv is named, then `aiml` from `PATH` —
  never a path of its own.  The venv comes before `PATH`: a build that links against one venv
  must not be compiled by another venv's compiler that happens to be first on the path.
- **F68-22** A program built against the venv links `-L$AIML_VENV/lib -laiml`.  If it is
  installed into `<venv>/bin`, it gets `RUNPATH $ORIGIN/../lib`; elsewhere it gets
  `RUNPATH $AIML_VENV/lib`.  `env` sets neither `LIBRARY_PATH` nor `LD_LIBRARY_PATH`: what a
  binary needs is in the binary, so it runs without the venv's environment too.
- **F68-23** **How a project says that** — in an `exodus.json`, two placeholders are expanded, next
  to the existing `__exodus_cache`:

  ```json
  "compiler": { "name": "$AIML" },
  "linker":   { "library_paths": ["$AIML_VENV/lib"],
                "flags": ["-Wl,-rpath,$ORIGIN/../lib"] }
  ```

  `$AIML` becomes the compiler of F68-21 (`$AIML` from the environment, else `aiml` from `PATH`),
  `$AIML_VENV` the venv root (`$AIML_VENV`, else the venv the resolved compiler belongs to,
  F68-31).  `$AIML_VENV` is substituted before `$AIML`, since one is a prefix of the other.
  **Nothing else is expanded**: `$ORIGIN` in a RUNPATH must reach the linker as those seven
  characters, so a general variable expansion would be wrong here.  A placeholder that cannot be
  resolved is an error naming `. <venv>/env` — not an empty string, which would silently build
  against whatever the host happens to have.  A project that writes a path into somebody's build
  tree instead (`../aiml/out/aiml/aiml`) keeps working; it just only builds where that tree is.

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
aiml venv <dir> install <name> --kontor <url> [--version <v>]      (§6.1)
aiml venv <dir> install --all --kontor <url>                       (§6.1, F68-66)
aiml venv <dir> publish [<name>] [--path <dir>] --kontor <url>     (§6.3)
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
  **`configs` names them instead** when `--all` is not what makes the tool: then each named
  configuration is built on its own (`$EXODUS build -c <file>`), in the order given, and the first
  failure ends the install.  cosmos is the reason the field exists — its `--all` is 62
  configurations, among them every test binary and five wasm builds that need an emsdk clone, so a
  tool that waited for `--all` to be green would never install.  A repository with one
  configuration says nothing and gets `--all`.
  A tool whose build configuration names a **build tree** instead (ufo's `exodus.json` has
  `"name": "../aiml/out/aiml/aiml"` and `-L../aiml/out/libaiml`) is refused with exactly that
  reason: it would be built by a foreign compiler and link against a library that is not in the
  venv.  Taking the compiler from `$AIML` and the runtime from `$AIML_VENV/lib` (F68-21, F68-22)
  is therefore the entry ticket for a tool, not a detail of its project.
- **F68-54** **The repository says what to install**, in `aiml.tool.json` at its root:

  ```json
  { "name": "cosmos", "version": "0.1.0",
    "configs":   ["exodus-cosmosd.json", "exodus-api.json"],
    "programs":  ["cosmosd", "cosmos-api"],
    "libraries": ["libcosmos_console_offer.so"],
    "package":   "." }
  ```

  `configs` is F68-53, `programs` go to `<venv>/bin/`, `libraries` to `<venv>/lib/`, and `package` names the directory
  of the source tree that becomes the importable package `lib/aiml/<name>/` (spec/21 §3.1), copied
  in and entered in the table with a path relative to `lib/aiml/` (F68-32).  Every key is optional
  except `name`; a tool may be only programs (cosmos) or only a package.  `package` is usually
  `"."` — the repository root, so that `@ufo/lib/registry.aiml` (spec/21 §3.1) means what it says.
  Copying it in **skips what the build left there** — `.git`, `out`, `build`, `__aiml_cache`,
  `__exodus_cache` and `*.o`: those are not part of the package, and a venv is a layer that must
  not grow by a build tree or differ per machine.  The `programs` and `libraries` of this install
  are skipped too — they are in `bin/` and `lib/` already, and a second copy inside the package
  would be the same bytes under a name nobody loads.  **No file → exit 1**
  („the repository has no aiml.tool.json"), and nothing is guessed: a venv that installed by
  guessing would not be the same venv as the next one.  The `name` must match the install's
  `<name>`.
- **F68-55** **The copies are relocatable.**  A program placed in `bin/` gets
  `RUNPATH $ORIGIN/../lib`, a library in `lib/` gets `RUNPATH $ORIGIN` — as F68-1 does for the
  compiler, and for the same reason: `. <venv>/env` is for people, a binary must run without it.
  Paths of the build host that the build left behind stay **after** `$ORIGIN`.  The install
  **checks and does not patch** — rewriting somebody's binary behind their back is not an
  install's business — and it checks only a copy that **needs the venv's runtime**: cosmos'
  `librt_shim.so` needs libc and nothing else, and a warning about that would teach people to
  ignore warnings.
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

### 6.1 kontor as a source

kontor keeps `/exodus/<name>/<version>/` — the `exodus.json` manifest and one `package` blob —
and that is where these tools are published (cosmos `doc/spec/111` §111.9).  `--kontor <url>`
fetches from there instead of cloning, so a machine needs no git server and no account to get the
toolbox: the venv, and a URL.

- **F68-60** **What kontor carries is source** (decided 2026-10-31).  The `package` blob is a
  reproducible source archive — ufo's `scripts/package.sh` writes one, the same bytes for the same
  sources — and the install builds it exactly as `--git` would (F68-53).  Not binaries: kontor's
  path has `<name>/<version>` and no place for an architecture, and a venv that can build is the
  whole point of F68-22.  A binary channel would need a word for the architecture first, and
  nothing here invents one.
- **F68-61** **The version.**  `--version <v>` names it.  Without it, `GET <url>/api/v1/graph`
  says which version of that project is the newest and that one is taken — the same answer kontor
  gives its own dependency graph, so nobody has to keep a list of versions by hand.
- **F68-62** **The fetch is the runtime's own HTTP client**, not a second one and not an external
  program: `GET <url>/exodus/<name>/<version>/exodus.json`, then the `package` blob streamed into
  `$AIML_CACHE/venv_tools/<name>/kontor-<version>/package.tar.gz` (F68-52 — nothing lands in the
  venv).  The manifest is asked for because it says whether that version is there at all; what it
  contains is not read here — what gets built is the archive, and what gets installed is said by
  the archive's `aiml.tool.json` (F68-54).  kontor keeps whatever the publisher PUT under that
  name, and for a repository with many configurations and no `exodus.json` (cosmos) that is its
  `aiml.tool.json`.  Anything but `200` ends the install naming the URL and the status.  The archive must
  unpack to **exactly one directory**; that directory is the tree that gets built.  An archive
  that unpacks into several top-level entries is refused rather than guessed at.
- **F68-63** **A token when the instance wants one.**  kontor's `anonymous` role is `reader` by
  default, so reading usually needs nothing.  Where it does, `$KONTOR_TOKEN` becomes an
  `Authorization: Bearer …` header — set in the request, never as an argument, because arguments
  are readable in `/proc` for anyone on the machine.
- **F68-64** **From there it is §6.**  The unpacked tree needs its `aiml.tool.json` like any other
  (F68-54), is built with `configs` or `--all` (F68-53), and places the same files in the same
  places.  A kontor install and a git install of the same version differ in where the tree came
  from, in nothing else.
- **F68-65** **The record** says `source: kontor:<url>` and, as the commit, `sha256:<digest>` over
  the package bytes: that digest is to kontor what a commit is to git — the one thing that says
  *which* bytes were built.  Installing the same digest again is the no-op of F68-57; `list` shows
  it and does **not** call it unreproducible, because those bytes can be fetched again.
- **F68-66** **`install --all --kontor <url>` installs the whole toolbox.**  Every project in
  kontor's graph, each at its newest version.  A project whose archive has no `aiml.tool.json` is
  **skipped with a line** — kontor is a repository for exodus projects, not a list of aiml tools,
  and a 9rooms in there must not stop the install of cosmos.  A build that fails is a failure, and
  the command ends non-zero after reporting; the closing line counts installed, skipped and
  failed.
- **F68-67** **What this is not.**  No signature is checked: whoever may write to that kontor
  decides what its name serves, and TLS is what protects the way there.  No mirror list, no
  downgrade protection, no "latest" that moves — `list` records the digest that was installed, and
  that is how you can tell afterwards what you have.

### 6.2 Every tool is built, none is interpreted (decided 2026-10-31)

- **A tool that was only ever run by the compiler gets an `exodus.json`.**  kontor had none; it
  was started as `aiml kontor.aiml -- 8081 ./data`, so there was no program to place and §6 could
  have installed it as a package only.  The owner's decision is the simple one: kontor becomes an
  ordinary project and is built ahead of time, like ufo and cosmos.  A manifest therefore has no
  `entry:` and `install` writes **no launchers** — `bin/` holds programs, not scripts that start a
  compiler.  Nothing in a venv is interpreted at run time.
- **The three repositories say what they install, and are built out of a venv.**  cosmos, kontor
  and ufo each carry an `aiml.tool.json`, and each `exodus.json` takes its compiler from `$AIML`
  and its runtime from `$AIML_VENV/lib` (F68-23) — cosmos too, with the consequence the owner
  accepted on 2026-10-31: **cosmos no longer builds without an active venv** (`. <venv>/env`),
  because an absolute path into somebody's build tree is exactly what F68-23 exists to end.
  Before that, installing ufo straight from its remote refused, naming the missing manifest
  (proved by hand on 2026-10-31 against `github.com/Pymann/ufo`: the clone works, the manifest was
  what was missing).

Tests: `tests/venv/run.sh` (install from a local path and from a git repository made on the fly;
the manifest's three kinds and its `configs`; RUNPATHs of the copies; the record, `list` and
`remove`; installing the same commit twice; a different commit replacing; a missing manifest, a
wrong `name`, a failing build, a missing `git`; nothing fetched into the venv).  §6.1 against
`tests/venv/kontor_stub.aiml` — kontor's three routes served by aiml itself, so the test needs no
kontor: a version named and a version found through the graph, the digest in the record, the
no-op on the same digest, `--all` with a project that is no tool, a 404 and an archive with two
top-level entries.

### 6.3 Publishing to kontor — `aiml venv <dir> publish` (decided 2026-11-25)

The way into kontor for what §6.1 takes out of it.  Before, a tool went there through a script of
its own repository (ufo `scripts/publish.sh`, cosmos `tools/kontor_publish.sh`); every aiml tool
can now be published the same way, from the venv it is built with, without a script in between.

```
KONTOR_TOKEN=… aiml venv <dir> publish [<name>] [--path <repo>] --kontor <url>
```

- **F68-70** **The repository says what it is**, in the same `aiml.tool.json` as for `install`
  (F68-54); for publishing, `name` **and** `version` are required.  `--path` is the repository's
  root (default: the current directory).  A `<name>` given must be the manifest's.
- **F68-71** **Two PUTs, in kontor's order.**  `PUT <url>/exodus/<name>/<version>/exodus.json`,
  then `PUT …/package` (kontor takes no package without a manifest).  The manifest is the
  repository's `exodus.json` where there is one, otherwise its `aiml.tool.json` (cosmos: many
  configurations, no `exodus.json`).  Where both exist they must say the same name and version —
  kontor files the package under the manifest's, `install` checks the archive's.  The requests
  are the runtime's own HTTP client (F68-62), TLS as there: a private CA via `AIML_WSS_CA`.
- **F68-72** **The package is what git tracks, committed.**  `git ls-files` decides what goes in
  (build trees, caches and run data stay out without a list of exclusions); a repository with
  uncommitted changes to tracked files is **refused**, because a published version cannot be
  replaced (F68-73) and must be something one can name afterwards.  `aiml.tool.json` and the
  manifest must be tracked.  The archive unpacks to **exactly one directory** `<name>-<version>/`
  (F68-62) and is **reproducible** — sorted, mtime 0, owner 0:0, `gzip -n` — so the same commit
  gives the same digest on every machine.  It is staged under `$AIML_CACHE/venv_publish/`, never in
  the venv (F68-52).  `git`, `tar` and `gzip` come from `PATH`; missing → exit 1, named.
- **F68-73** **A published version is not overwritten.**  Before anything is sent, `publish` asks
  for `…/package`; `200` ends it („already in kontor — raise the version").  kontor enforces the
  same on its side: once a version's package is there, a PUT of its manifest or package is `409`
  (kontor `README`), so neither a second `publish` nor a hand-made `curl` replaces bytes that
  somebody may have installed.  A half publish (manifest there, package not) can be repeated.
- **F68-74** **The token.**  `$KONTOR_TOKEN`, a kontor token with the role `writer`, becomes the
  `Authorization: Bearer` header as in F68-63 — never an argument.  Without one, `publish` refuses
  before it touches the network; a `401`/`403` is reported with kontor's own message.
- **F68-75** **What this is not.**  No build before the upload (that is CI's business and the
  install's, F68-64), no signature, no automatic publish from CI (decided 2026-11-25: by hand for
  now).  Reading is the instance's affair: a kontor whose `anonymous` role is `none` needs
  `$KONTOR_TOKEN` for `install --kontor` too (F68-63); a tool without a token is installed with
  `--git`.

Tests: `tests/venv/run.sh` §6.3 against `tests/venv/kontor_stub.aiml`, which also takes kontor's
two PUTs (with a token, and `409` for a published version): no token, a wrong token, a wrong name,
`exodus.json` and `aiml.tool.json` disagreeing, uncommitted changes, the files where `install`
looks, one tree of tracked files only, the digest in the line, nothing staged in the venv, a second
publish refused with the bytes unchanged, the next version beside it, and an install of what was
published.
