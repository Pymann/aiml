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
  lib/aiml/aiml.assets.json      the asset packages `asset install` placed (§9)
  assets/<name>/…                asset packages, one directory each, the venv's asset root (§9)
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
- **F68-21** `AIML` names the compiler.  A tool that runs the compiler (`aiml build`, test drivers)
  takes `$AIML`, then `$AIML_VENV/bin/aiml` when a venv is named, then `aiml` from `PATH` —
  never a path of its own.  The venv comes before `PATH`: a build that links against one venv
  must not be compiled by another venv's compiler that happens to be first on the path.
- **F68-22** A program built against the venv links `-L$AIML_VENV/lib -laiml`.  If it is
  installed into `<venv>/bin`, it gets `RUNPATH $ORIGIN/../lib`; elsewhere it gets
  `RUNPATH $AIML_VENV/lib`.  `env` sets neither `LIBRARY_PATH` nor `LD_LIBRARY_PATH`: what a
  binary needs is in the binary, so it runs without the venv's environment too.
- **F68-23** **How a project says that** — in a build configuration (`exodus.json`, §7), two
  placeholders are expanded:

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
aiml venv <dir> install --all --path <dir>                         (F68-68)
aiml venv <dir> publish [<name>] [--path <dir>] --kontor <url> [--overwrite]   (§6.3)
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
- **F68-53** **The build is the tool's own.**  In the fetched tree, `install` runs the venv's own
  `aiml build --all` (§7) — no exodus, no Python (decided 2026-11-25) — with `AIML_VENV`, `AIML`
  and `PATH` pointing at the **target venv**: a tool is built by the compiler it will live next
  to, and links against that venv's runtime (F68-22).  The build runs as a child process of the
  target venv's `bin/aiml`, so an install from one venv into another uses the target's compiler.
  A failed build installs nothing (exit 1, the build's output is passed through).
  **`configs` names them instead** when `--all` is not what makes the tool: then each named
  configuration is built on its own (`aiml build -c <file>`), in the order given, and the first
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
- **F68-58** `install` **does not touch the system, and brings the libraries itself**
  (revised 2026-11-26).  No apt run as root, no `/etc`, no `sudo` — but a tool that needs system
  libraries no longer needs a base image that has them.  It pins them as `apt_packages` in its
  project (cosmos: libldap, xmlsec1); its build fetches them into the build cache (F68-100, the
  user's own cache, no root), and the install places every shared library a built program or
  library **loads from that cache** into `<venv>/lib/` (F68-58a).
- **F68-58a** **Which libraries.**  After the build, the install asks the loader which libraries
  each placed program and library resolves (`ldd`), and copies those whose path lies inside the
  build cache (`$EXODUS_CACHE`, else the tree's `__exodus_cache`) into `<venv>/lib/` under the name
  the program asks for (its SONAME), the real file, not the link.  A library the loader finds
  in the system stays the system's.  The loader and libc (`ld-linux*`, `libc.so.6`, `libm`,
  `libpthread`, `libdl`, `librt`) are never copied: they belong to the system's loader, and a libc
  that does not match it does not start.  The copies are files of the install: the record
  (F68-56) lists them, `remove` deletes them, and an update replaces them.  A file of that name
  that another install placed already stays and is shared; one that a different install placed
  with **different bytes** ends the install naming both tools.  With F68-55's
  `RUNPATH $ORIGIN/../lib` the program finds them in the venv, also on a machine without the
  cache.  No `ldd` (a libc without it) → nothing is copied, and the install says so in one line.
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
  default, so reading usually needs nothing; an instance that must not be read without a token
  sets `auth.anonymous: none` (decided 2026-11-26: this stays the instance's configuration, not a
  default of kontor or of `install`).  Where a token is needed, `$KONTOR_TOKEN` becomes an
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
- **F68-68** **`install --all --path <dir>` installs every tool in a directory.**  `<dir>` itself,
  if it carries an `aiml.tool.json`; otherwise each directory directly inside it, sorted by name,
  that carries one — the local counterpart of F68-66, for a checkout of several tool repositories
  next to each other.  A directory inside it without `aiml.tool.json` is skipped with a line.  Each
  tool is installed as `install <name> --path <its directory>` (F68-50) under the `name` of its
  `aiml.tool.json`; a failure is reported and the next tool is installed, and the closing line
  counts installed, skipped and failed (exit 1 when one failed).
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
KONTOR_TOKEN=… aiml venv <dir> publish [<name>] [--path <repo>] --kontor <url> [--overwrite]
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
  A version kontor **deleted** (a tombstone, `GET` answers `410`, kontor FW-47) is refused the
  same way, with its own reason („was deleted — a deleted version number is not reused"):
  kontor keeps the number from being given out again.
- **F68-74** **The token.**  `$KONTOR_TOKEN`, a kontor token with the role `writer`, becomes the
  `Authorization: Bearer` header as in F68-63 — never an argument.  Without one, `publish` refuses
  before it touches the network; a `401`/`403` is reported with kontor's own message.
- **F68-75** **What this is not.**  No build before the upload (that is CI's business and the
  install's, F68-64), no signature, no automatic publish from CI (decided 2026-11-25: by hand for
  now).  Reading is the instance's affair: a kontor whose `anonymous` role is `none` needs
  `$KONTOR_TOKEN` for `install --kontor` too (F68-63); a tool without a token is installed with
  `--git`.
- **F68-76** **An admin may overwrite** (decided 2026-11-25, kontor `doc/spec/web_ui.md`
  FW-47a).  `--overwrite` skips the refusal of F68-73 for a version that is there (`200`) or was
  deleted (`410`) and sends both PUTs anyway; **kontor** decides.  It replaces them only for a
  token with the `admin` right and answers everyone else `409` as before — `publish` then names
  the admin right instead of F68-73.  The line says `replaced` instead of `published`.  The flag
  belongs to `publish` alone (anywhere else it is a usage error), and it changes nothing about
  F68-72: what goes out is still one committed state.  A venv that installed the old bytes keeps
  them; its record (F68-65) names their digest, so `list` shows which one it has.

Tests: `tests/venv/run.sh` §6.3 against `tests/venv/kontor_stub.aiml`, which also takes kontor's
two PUTs (with a token, and `409` for a published version): no token, a wrong token, a wrong name,
`exodus.json` and `aiml.tool.json` disagreeing, uncommitted changes, the files where `install`
looks, one tree of tracked files only, the digest in the line, nothing staged in the venv, a second
publish refused with the bytes unchanged, `--overwrite` refused for a writer and done for an
admin (F68-76), `--overwrite` on `install` refused, a deleted version refused with its reason and
written again by the admin, the next version beside it, and an install of what was published.

## 7. Building — `aiml build` (decided 2026-11-25)

A tool used to be built by exodus, a Python build tool, so a venv that could install had to have
Python and exodus next to it.  What an aiml **tool** needs from it is two steps — compile the entry
file, link the objects — and those the compiler does itself.  Since 2026-11-26 `aiml build` also
does what exodus did for **aiml itself**: it fetches the pinned apt packages (§7.2) and git
packages (§7.3), compiles C and C++ sources and archives static libraries (§7.4), so the
compiler, its runtime and its wasm32 runtime build from their own project files without exodus
and without Python.

```
aiml build [-c <config.json>]… [--all]
```

- **F68-80** **The configuration is the one the projects already have.**  An exodus project file
  (`"$schema": "exodus.project.config-1.0"`) is read as it is, so cosmos, kontor and ufo keep
  their files and their names.  Without `-c` the file is `./exodus.json`; `-c` may be given more
  than once and builds in that order; `--all` builds every `*.json` in the working directory that
  carries that `$schema`, sorted by file name.  Every path in it is relative to the **working
  directory** (exodus' `source_root: "."`), also for a file in a subdirectory
  (cosmos `runtime/exodus-rt.json`).
- **F68-81** **What is read**: `name`, `output_type` (`executable`, `shared_lib`, `static_lib`,
  `wasm`), `sources` (`.aiml` and C/C++ files and glob patterns of them — `**` matches any number
  of directories, as in exodus; a pattern that matches nothing adds nothing, as in exodus, only a
  path that is not there and a configuration left with no source at all are errors — and `.o`
  files taken as they are), `compiler.name`,
  `search_paths` (`-I`) and `defines` (`-D`) as exodus passed them, `linker.name` (default `cc`,
  `ar` for `static_lib`), `linker.flags`, `linker.libraries`, `linker.library_paths`,
  `linker.rpath` (default `true`), `linker.script` (`-T`), `linker.map_file` (`-Wl,-Map=`),
  `artifact_in_cwd`, `asset_directories`, `env`, `apt_packages` (§7.2), `git_packages` (§7.3),
  `version`, `license`, `pre_compilation`, `pre_linkage` and `post_linkage` (F68-87).  For C/C++
  sources also `compiler.flags`, `compiler.lang_standard`, `optimization`, `debug`, `warnings` and
  `werror` (F68-111).  For an `.aiml` source exodus passed none of these on, so they do not reach
  the aiml compiler.  `jobs` and `-j <n>` set how many compiles run at once (F68-112).  Every other
  field (`misra_*`, `clang_*`, `architecture`, `src_pattern_for_headers`, …) is read by no build
  step and ignored.  `compiler.version` and the `version` of each `dependencies` entry are read as
  pins (§7.5, F68-114); the rest of `dependencies` is not used by the build.
- **F68-82** **What is refused, by name** (revised 2026-11-26): `conan_packages` that are not
  empty, a hook that is no `.aiml` file (F68-87), and a source that is neither `.aiml`, `.o`, nor
  C/C++ (`.c`, `.cc`, `.cpp`, `.cxx`).  A build that silently skipped them would build something
  else; the message names the field.  `apt_packages` (§7.2), `git_packages` (§7.3), C/C++ sources
  and `output_type: static_lib` (§7.4) and paths into `__exodus_cache` (F68-101) were refused
  until 2026-11-26 with „needs exodus" and are built now.
- **F68-83** **Compile**: for each `.aiml` source,
  `<compiler> <source> -o <build>/<source without .aiml>.o [--asset-roots=<dirs>]` with
  `<build> = out/<name>/` (exodus' `build_root`).  The compiler writes the objects of the modules
  the source imports next to that object (`<dir>_<file>_aiml.o`); the build directory is emptied
  first, so every `.o` in it belongs to this build and **all of them are linked**.  Incremental
  work is the compiler's object cache (`$AIML_CACHE`, spec/29_2 §12), not a second one here.
  A configuration with C/C++ sources keeps its build directory instead (F68-112); its `.aiml`
  sources then compile into `<build>/aiml/`, which is emptied the same way and all of whose
  objects are linked.
- **F68-84** **Link**: `<linker> <objects> -o <output> [-shared] <linker.flags>
  -L<path>… [-Wl,-rpath,<path>]… -l<lib>…`, in that order, as exodus did — so a RUNPATH that
  starts with `$ORIGIN` from `linker.flags` stays first (F68-55).  An rpath entry containing `:`
  goes through a colon-free alias (F68-102).  The output is `<name>` (`lib<name>.so` for
  `shared_lib`, `lib<name>.a` for `static_lib` (F68-113), a `lib` prefix not doubled) in the
  working directory with `artifact_in_cwd`, else in `<build>`.  `linker.script` adds
  `-T<script>` and `linker.map_file` `-Wl,-Map=<file>` after the flags.  The linker stays an external program — `g++`/`cc`, whatever `linker.name`
  says — and missing → exit 1, named.
- **F68-85** **Placeholders** are those of F68-23 and `__exodus_cache` (F68-101), nothing else:
  `$AIML_VENV`, then `$AIML`, in every compiler/linker name, flag, path and `env` value; `$ORIGIN`
  reaches the linker as those seven characters.  An unresolvable placeholder ends the build naming `. <venv>/env`.  `env`
  is set for the compile and the link.
- **F68-86** **No shell.**  Every command is run with its arguments as given (the `runProg` of
  §6), so nothing in a configuration is interpreted.  Each command is printed before it runs.
  Exit 0 when every configuration built; otherwise the first failure ends the build with exit 1,
  naming the configuration.
- **F68-87** **Hooks are aiml, run by the JIT.**  exodus' hooks were Python modules whose
  `run(config)` it imported; here a hook is an `.aiml` file, run as
  `<compiler> <hook.aiml>` (JIT, no `-o`) in the working directory: `pre_compilation` before the
  first compile, `pre_linkage` after the last compile and before the link, `post_linkage` after a
  successful link (new — exodus had none).  The hook sees the configuration's `env` and
  `AIML_BUILD_STAGE` (`pre_compilation` / `pre_linkage` / `post_linkage`), `AIML_BUILD_CONFIG`
  (the configuration file), `AIML_BUILD_NAME`, `AIML_BUILD_DIR` (the object directory, F68-83)
  and `AIML_BUILD_OUTPUT` (the linked file, F68-84).  A hook works on files and does **not**
  change the configuration: what a Python hook did to `config` in memory has no counterpart, and a
  build whose inputs are rewritten by a script while it runs could not be read from its file.
  Its exit code is the hook's verdict: non-zero ends the build, naming the hook and the stage.
  A hook that is named but missing is an error (exodus skipped it silently, and a stamp that was
  never written then looks like a stamp that was).  A `.py` hook is refused with „port it to an
  `.aiml` hook" (F68-82).

### 7.1 A browser build — `output_type: wasm` (2026-11-25, proposed)

A browser client (cosmos' web client, kontor's web UI) is an aiml program compiled for wasm32
and linked by emscripten against the wasm32 runtime of §8.  exodus did that; `aiml build` does
it the same way, so the last exodus project files of the tools (cosmos `exodus-client.json` and
its wasm tests) build without exodus too.

- **F68-88** **Compile and link as exodus did.**  Each `.aiml` source is compiled with
  `--target wasm32 --emit-main` to `<build>/<source>.ll` (LLVM IR — emcc does not link native
  objects); the imports' IR lands next to it (`<dir>_<file>_aiml.ll`), and every `.ll` in the
  emptied build directory is linked, like the `.o` of F68-83.  A native `.o` source is refused.
  The linker is `linker.name`, else `$EMCC`, else `emcc` from `PATH`.  The link line is
  `<emcc> <.ll…> -o <name>.html --preload-file=<asset_directory>… <linker.flags> -L<path>…
  -l<lib>…` — no `-shared`, no `-Wl,-rpath` (a wasm module loads nothing at run time).  The
  output is `<name>.html`, and emcc writes `<name>.js` and `<name>.wasm` beside it, in the working
  directory with `artifact_in_cwd`, else in `<build>`.  The runtime is named like any library:
  `"library_paths": ["$AIML_VENV/lib/wasm32"], "libraries": ["aiml_wasm"]` (F68-94 puts it
  there), with the flags of F68-92.  Hooks (F68-87) run as for a native build.
- **F68-89** **One emsdk.**  When a library path holds `libaiml_wasm.a` **and** its
  `emsdk.version` (F68-90), the build asks the linker for `--version` before compiling anything;
  another version ends the build naming both („built with emsdk 3.1.74, but emcc is 4.0.1 —
  activate emsdk 3.1.74").  An archive and an emcc of different versions do not link reliably
  (§8), and a mismatch found by the linker an hour later names neither.  A linker that cannot be
  run ends the build the same way, naming `$EMCC`.  The emsdk is the environment's
  (`. <emsdk>/emsdk_env.sh`, or `$EMCC`) or a `git_packages` entry of the configuration (§7.3,
  since 2026-11-26) whose path `linker.name` names — aiml's own `exodus-wasm-runtime.json` pins
  emsdk that way.

### 7.2 apt packages — `apt_packages` (decided 2026-11-26)

What `exodus pkg install` and `tools/ci/fetch_pinned_debs.py` did for the pinned Ubuntu packages,
the build does itself, before anything is compiled — in the user's own cache, without root.

- **F68-100** **The cache is exodus'.**  An entry `{ "name", "version", "arch", "digest",
  "required" }` is a `.deb` at `<cache>/apt/<name>/<arch>/<version>/_download/` and its unpacked
  content at `…/payload/`.  `<cache>` is `$EXODUS_CACHE`, else `<working directory>/__exodus_cache`
  — the directory exodus fills, so either tool uses what the other fetched.  A package whose
  `payload/` exists next to a `.deb` with the pinned digest is used as it is, nothing is fetched.
- **F68-100a** **Fetching** (every package not present, before the `pre_compilation` hook):
  `apt-get download <name>:<arch>=<version>` in `_download/` where `apt-get` is on `PATH` — it reads
  the mirror index and needs no root; when it finds nothing (Ubuntu drops superseded point
  releases from the index), the permanent copy `<base>/<name>_<version>_<arch>.deb`, then
  `…_all.deb`, with the runtime's own HTTP client (proxy from the environment, redirects).  The
  version in the file name is without its epoch (`1:18.1.3-1ubuntu1` → `18.1.3-1ubuntu1`).
  `<base>` is `$AIML_DEB_URL`, else `https://launchpad.net/ubuntu/+archive/primary/+files`.  The
  `.deb` is unpacked with `dpkg-deb -x`, else (no dpkg) with `ar x` and `tar -xf data.tar.*`.
- **F68-100b** **The digest decides.**  The sha256 of the `.deb` must be the pinned `digest`; a
  different one ends the build naming both sums — nothing unverified is unpacked.  A fetch goes
  to a directory of its own and replaces the cached entry only once it is verified and unpacked:
  a cached entry whose `.deb` is not the pinned one is fetched again, and if that fails the build
  ends and the cache is **left as it was** (exodus' cache holds such entries; they are not this
  build's to delete).  An entry without `digest` is used, and one line names the sum to pin.
  The build **does not write its configuration** (`exodus pkg install` wrote `digest` back): a
  build whose file changes under it could not be read from that file.
- **F68-100c** **`required: false`**: a package that cannot be fetched is skipped with a line.
  A required one ends the build, naming the package and both places it looked.
- **F68-100d** **Links across packages.**  A `-dev` package's `libfoo.so → libfoo.so.1` points into
  its runtime package, which is unpacked into another payload, so the link is broken.  Once every
  package is present, a broken link in a payload of the configuration whose target's file name is
  a file in another of its payloads is made to point there (absolute); one whose target is in
  none of them is removed, with a line naming it — the runtime package is missing from the
  configuration (exodus' rule).
- **F68-100e** **Paths are the configuration's.**  The `search_paths` and `library_paths` that
  name payload directories are written in the configuration (`exodus pkg install` put them
  there); the build adds no path of its own, and it resolves no `Depends:` — the configuration
  pins every package it needs, so two builds of one file fetch the same packages.
- **F68-101** **`__exodus_cache`** in a compiler or linker name, a flag, a path or an `env` value
  becomes the absolute `<cache>`, as exodus' `_expand_cache` did.  It may also name a file of a
  git package (F68-107).
- **F68-102** **A colon in an rpath.**  Debian versions with an epoch put a `:` into a cache path
  (`…/libllvm18/amd64/1:18.1.3-1ubuntu1/…`), and `:` separates RUNPATH entries.  Such a
  `library_paths` entry gets the alias `<cache>/rpath/<its path below the cache, ':' written as
  %3a>`, a symlink to the real directory, and the rpath takes the alias.  A colon path outside the
  cache cannot be aliased and is left out of the rpath with a line.

### 7.3 git packages — `git_packages` (decided 2026-11-26)

- **F68-105** **Where.**  An entry `{ "name", "repo", "ref", "digest", "setup_commands",
  "required" }` is a clone at `<cache>/git/<name>/<commit>/`.  With a `digest` (a commit) whose
  directory exists, nothing is fetched.  Otherwise `git clone --depth 1 [--branch <ref>] <repo>`
  (`ref` `HEAD` or empty: the default branch); when a `digest` is pinned and the clone is at
  another commit, `git fetch --depth 1 origin <digest>` and `git checkout --detach <digest>`
  follow, and a commit that cannot be fetched ends the build naming both.  Without `digest` one
  line names the commit to pin.  `git` comes from `PATH`; missing → named.
- **F68-106** **`setup_commands`** run in the clone, in order, each an argv without a shell
  (F68-86) — after the clone, and again in a later build only while they have not all succeeded
  once (a marker `.aiml_setup_done` in the clone says they have).  A failing command ends the build
  naming it (`required: false`: the package is skipped with a line).
- **F68-107** **`current`.**  `<cache>/git/<name>/current` is a link to the commit directory, so a
  configuration says `__exodus_cache/git/emsdk/current/upstream/emscripten/em++` without the
  commit (exodus' link).
- **F68-108** Git packages are fetched after the apt packages and before the `pre_compilation`
  hook, in the order given.

### 7.4 C and C++ sources, static libraries (decided 2026-11-26)

- **F68-110** **Sources** with the extension `.c`, `.cc`, `.cpp` or `.cxx` are compiled with
  `compiler.name` (default `cc`) to `<build>/<source path, extension replaced by .o>`.
- **F68-111** **The compile line is exodus':**  `<compiler> <compiler.flags> -c <source> -o <object>
  -MMD -MP [-std=c++<lang_standard> | -std=c<lang_standard>] -O<optimization> [-g] -W<w>…
  [-Werror] -I<search_path>… -D<define>…`, with exodus' defaults (`optimization` `"0"`, `debug`
  true, `warnings` `["all", "extra"]`, `werror` false).  `-std` is `c++…` for `.cc`, `.cpp`,
  `.cxx` and `c…` for `.c`.
- **F68-112** **Incremental and parallel.**  A configuration with C/C++ sources keeps its build
  directory.  An object is compiled again when it is missing, or older than its source, than the
  configuration file, than the compiler (when `compiler.name` is a path), or than a file its
  `.d` names, or when a file the `.d` names is gone; otherwise one line says it is up to date.
  `-j <n>` (or `jobs`; default: the number of processors) compiles run at once.  The first
  failure lets the running compiles finish, links nothing and ends the build naming the source.
  The link takes the sources' objects in source order, then the objects of `.aiml` sources
  (F68-83).
- **F68-113** **`output_type: static_lib`** is `<linker> rcs <output> <objects>` with `linker.name`
  (default `ar`; `emar` for wasm32 objects).  The archive is removed first, so a source that left
  the configuration leaves no member behind.  No flags, libraries or rpath — an archive links
  nothing.

Tests: `tests/build/run.sh` — an executable and a shared library from one `.aiml` file that
imports a module (the side object is linked), `-c` several times and `--all` by `$schema`, a
configuration in a subdirectory with paths relative to the working directory, `artifact_in_cwd`
on and off, the RUNPATH starting with `$ORIGIN`, `env` reaching the compiler, an unresolvable
`$AIML_VENV`, each refusal of F68-82, a compile error and a missing linker; the three hooks in
order with their variables, a hook's file seen by the compile, a failing hook ending the build, a
missing hook and a `.py` hook refused; for wasm (F68-88/89) against a stand-in emcc the compile
line, the `.ll` of module and import on the link line, `--preload-file`, no rpath, an emcc of
another emsdk refused, a native object and `git_packages` refused — and, where the CI has emsdk
and the wasm32 runtime, a real build that node runs with the module's output and exit code.
§7.2–§7.4: C and C++ sources with `lang_standard`, defines and include paths, a second build that
compiles nothing, a touched header that compiles its source again, a `static_lib` linked into a
program, `-j`; apt packages from a local archive over HTTP (`$AIML_DEB_URL`): fetched and unpacked
into exodus' layout, the digest checked (a wrong one refused, nothing unpacked), a cached package
not fetched again, a `-dev` link repaired across payloads, an optional package that is not there
skipped, a colon path aliased in the RUNPATH; a git package from a local repository at a pinned
commit with its setup command run once, `current`, and its file used through `__exodus_cache`.
`tests/venv/run.sh` installs with `PATH` free of exodus and Python, `install --all --path` over a
directory of tools (one without `aiml.tool.json` skipped), and a tool whose library comes from an
apt package: the library lands in `<venv>/lib/`, the program runs with the cache deleted, and
`remove` takes the library out again.

### 7.5 Version pins — `compiler.version` and `dependencies[].version` (decided 2026-11-26)

A tool is built against one aiml and works next to certain versions of its neighbours (ufo,
kontor).  Until now that knowledge lived outside the repository: in a CI variable that falls back
to the newest release, or nowhere.  A green build then proves nothing about the aiml it will meet
in a venv, and an incompatibility is found by whoever runs it next.  The pin belongs into the
build configuration, versioned with the code it describes, and a mismatch must be said at once,
not after an hour of compiling.

```json
"compiler": {
  "name": "$AIML",
  "version": { "ge": "1.0.2.7", "lt": "1.1" }
},
"dependencies": [
  { "name": "ufo",    "version": { "eq": "0.1.0" } },
  { "name": "kontor", "version": { "ge": "0.1.0", "lt": "0.2" } }
]
```

- **F68-114** **No new file, no new format.**  The pin lives in the exodus project file the tool
  already has (F68-80): `compiler.version` for the compiler, `version` of a `dependencies` entry
  for a neighbour.  The format keeps its `$schema` (`exodus.project.config-1.0`): both keys are
  optional, a file without them builds exactly as before.  Only the configuration that builds the
  tool's main program needs a pin; a repository with many configurations (cosmos) pins in that
  one, and the others build unchecked.
- **F68-115** **A range is an object of comparisons**, the keywords of aiml's own comparisons:
  `eq`, `gt`, `ge`, `lt`, `le`.  Several keys hold together (and): `{ "ge": "1.0.2.7", "lt":
  "1.1" }` is a range.  `eq` stands alone — `eq` next to another key is an error, and so is a
  range that admits nothing (`{ "gt": "2", "lt": "1" }`).  A bare string is **not** shorthand for
  `eq`: a pin is always written as what it is.  An unknown key is an error naming it (`"gte"` must
  not become a pin that never applies).
- **F68-116** **Comparison is numeric, segment by segment.**  A version is dot-separated
  non-negative integers; a leading `v` is dropped, so the tag `v1.0.2.7` and `1.0.2.7` are the same
  version.  A missing segment counts as 0: `lt: "1.1"` means below `1.1.0.0`, and `1.0.10` is
  greater than `1.0.9`.  Anything else in a pin (`1.0-rc1`, `latest`, a sha) is an error in the
  configuration; it is not compared as text.
- **F68-117** **The compiler knows its version.**  The release carries it — `aiml --version` prints
  the version the package step was given (`v1.0.2.7`), and the venv's runtime directory carries
  the same line as `lib/aiml.version`, copied by `aiml venv` like `wasm32/` (F68-94).  A build
  from a commit (`git-<sha>`, F68-90's packaging of untagged runs) has **no comparable version**:
  every pin on it counts as a mismatch, with that reason, so a pin is never silently satisfied by
  a build nobody released.
- **F68-118** **A mismatch warns at once and does not stop the build.**  `aiml build` checks the
  pins of a configuration **before compiling anything** (as F68-89 does for emsdk) and writes one
  line per mismatch to stderr, in a fixed form that a host can recognise:

  ```
  aiml: version-mismatch compiler aiml 1.0.3.0 not in {ge 1.0.2.7, lt 1.1} (exodus-cosmosd.json)
  aiml: version-mismatch dependency ufo 0.2.0 not in {eq 0.1.0} (exodus-cosmosd.json)
  ```

  The build goes on and its exit code is unaffected: an incompatibility is a **signal**, not a
  failure — the pin may be stale, and a tool that builds and passes its tests against a newer aiml
  is information, not breakage.  What a host does with the line is the host's: cosmos marks the
  step magenta (spec/62 F62-47).  A dependency is checked against the version installed in the
  active venv (`aiml venv <dir> list`); one that is not installed is a mismatch with that reason.
- **F68-119** **exodus reads the same keys.**  exodus' model accepts `compiler.version` and a range
  as `Dependency.version`, with the rules of F68-115 and F68-116, and warns the same way.  Two readers
  of one format must not disagree on what a pin means.

Tests: `tests/build/run.sh` — a configuration whose pin admits the running compiler builds without
a line; one whose pin excludes it builds and prints exactly one `version-mismatch` line; `eq`
with `lt`, an unknown key, an empty range and `1.0-rc1` are each refused naming the key; `v1.0.2.7`
equals `1.0.2.7`; `1.0.10` is greater than `1.0.9`; a `git-<sha>` compiler mismatches every pin.

## 8. The wasm32 runtime in the release (decided 2026-11-25)

A program that runs in the browser is compiled by the same compiler (`--target wasm32`) and
linked by emscripten against `libaiml_wasm.a`, the runtime built from
`exodus-wasm-runtime.json`.  A project that builds a browser client from the release (kontor's
web UI) needs that archive and must know which emsdk made it, because an archive and an `emcc` of
different versions do not link reliably.

```
aiml-<version>-linux-x86_64/
  lib/wasm32/libaiml_wasm.a      the wasm32 runtime
  lib/wasm32/emsdk.version       one line: the emsdk version it was built with, e.g. 3.1.74
```

- **F68-90** The release tarball (and the `aiml-linux-x86_64` CI artifact) carries
  `lib/wasm32/libaiml_wasm.a` and `lib/wasm32/emsdk.version`.  Packaging fails when the archive
  was not built: a release without it is not made.
- **F68-91** `emsdk.version` is the version `exodus-wasm-runtime.json` installs (its emsdk
  `setup_commands`), not a guess: packaging reads it from there.
- **F68-92** The link line a project uses (the one `tools/run_wasm_test.sh` uses):
  `aiml prog.aiml --target wasm32 --emit-main -o prog.ll`, then
  `emcc prog.ll -o prog.js -L<lib/wasm32> -laiml_wasm -fexceptions -sASYNCIFY=1
  -sALLOW_MEMORY_GROWTH=1` plus the project's own `-sENVIRONMENT=…`.
- **F68-93** Packaging proves it: it compiles the one-line program `- call: [sys.exit, 42]`,
  links it against the **packaged** archive (not `out/`) with the line of F68-92 and the emsdk
  named in `emsdk.version`, and runs it with that emsdk's node; the exit code must be 42, like
  the native smoke test.
- **F68-94** `aiml venv <dir>` copies `lib/wasm32/` along when the runtime it takes has a
  `wasm32/` directory next to it (the release layout and a venv made from a venv), so
  `$AIML_VENV/lib/wasm32` is where a project finds it.  Without one, nothing is copied and
  nothing fails: a venv for native tools does not need it.

Tests: packaging itself (F68-90, F68-91, F68-93) in CI; `tests/venv/run.sh` for F68-94 — a
release-shaped directory with `lib/wasm32/` gives a venv with the same two files, one without it
gives a venv without `lib/wasm32/`.

## 9. Asset packages — `aiml venv <dir> asset …` (decided 2026-11-26)

Assets (spec/44_7) that a project does not keep in its own repository — a texture set, a sound
bank, a font family — come into a venv as **asset packages**: blobs in kontor's generic store,
fetched by name and version like a tool (§6.1), and found by every compile and every run of the
venv's compiler without a line in the project's configuration.

```
aiml venv <dir> asset install <name> --kontor <url> --version <v>
aiml venv <dir> asset install <name> --path <archive.tar.gz>
aiml venv <dir> asset publish <name> --version <v> --path <dir> --kontor <url> [--overwrite]
aiml venv <dir> asset list
aiml venv <dir> asset remove <name>
```

```
<venv>/assets/                   the one asset root of the venv (F68-121)
  <name>/…                       one directory per asset package, as its archive has it
<venv>/lib/aiml/aiml.assets.json what `asset install` placed, and where it came from (F68-126)
```

- **F68-120** **What an asset package is.**  One `tar.gz` in kontor's **generic** store at
  `<url>/generic/<name>/<version>/<name>.tar.gz` — not `/exodus/…`, because an asset package is
  no project: it has no `exodus.json`, nothing is built from it, and it only carries files.  The
  archive's root **is** the package: `icons/hammer.png` in the archive is
  `<venv>/assets/<name>/icons/hammer.png` (no wrapper directory, unlike F68-62).  It holds
  directories and regular files of the kinds of spec/44_7 and nothing else.
- **F68-121** **One root, one directory per package.**  Every package lies in
  `<venv>/assets/<name>/`, and `<venv>/assets` is the venv's **one** asset root: the compiler of
  the venv (F68-31: found from its own image, not from `AIML_VENV`) adds it to the roots of
  `--asset-roots` on every compile and every JIT run, when the directory exists.  The scanner
  walks it recursively, so lookup stays by stem (`gl.tex("hammer")`) and the directory a file
  lies in means nothing to the code; the directory per package is for the venv — an update
  replaces it and a removal deletes it, with no file list to keep and no file of an older
  version left behind to be found.  Project assets and venv assets are one store: a stem in both
  is the duplicate error of spec/44_7 at compile time.
- **F68-122** **Names are snake_case — every one.**  The package name is `[a-z][a-z0-9]*(_[a-z0-9]+)*`;
  every directory in the archive and every file's stem is `[a-z0-9]+(_[a-z0-9]+)*` (lowercase
  letters and digits in words joined by single underscores: no uppercase, no `-`, no space, no
  leading, trailing or double `_`).  A file is `<stem>.<ext>` with exactly one dot and an
  extension spelled in lowercase as spec/44_7 lists it.  The version is
  `[0-9A-Za-z][0-9A-Za-z.+_-]*`.  The scanner merely warns about and skips an uppercase name
  (spec/44_7); a package with one is **refused as a whole**, naming every offending path, because
  a package that loses files on the way into the store is not the package that was published.
- **F68-123** **No duplicates.**  A stem is unique **per kind** (spec/44_7's stores: texture,
  audio, shader, font, data, mesh) — within the package (`icons/hammer.png` and
  `items/hammer.webp` are two textures `hammer`) and across all packages of the venv.  The same
  stem in two kinds (`golem.png`, `golem.ogg`) is allowed, as in spec/44_7.  A duplicate is
  refused naming both paths and, across packages, both packages; nothing is placed.
- **F68-124** **What else is refused**: an empty package, a symbolic link, a hard link that
  unpacks as a second name, any entry that is not a directory or a regular file, a file whose
  extension is no asset kind (spec/44_7's "warn + skip" is for a project's working tree, not for
  a package), and everything the untar of spec/33_2 §3.7a refuses (an absolute path, `..`).
- **F68-125** **The rules hold when publishing.**  `asset publish` checks F68-122 to F68-124 on
  `--path <dir>` **before it touches the network**, and F68-123 also against the other packages
  installed in `<dir>`'s venv, so a package that cannot be installed is never published.  The
  archive is reproducible as in F68-72 (sorted, mtime 0, owner 0:0, `gzip -n`), its root the
  directory's content.  A version that kontor has (`GET` answers `200`) is refused as in F68-73 —
  kontor's generic store replaces on `PUT`, so the refusal is the client's — unless
  `--overwrite`.  The `PUT` carries `$KONTOR_TOKEN` as in F68-74 and `X-Checksum-Sha256`, so
  kontor stores exactly the bytes that were checked.  The line names files, bytes and digest.
- **F68-126** **Installing.**  `--kontor`: the blob is streamed into
  `$AIML_CACHE/venv_assets/<name>/<version>/<name>.tar.gz` (F68-52: nothing is fetched inside the
  venv), `--path`: that archive is taken as it is.  It is unpacked into a staging directory in
  the same cache, checked (F68-122 to F68-124, F68-123 against every **other** installed
  package), and only then moved into place: the new tree is renamed to `<venv>/assets/<name>`,
  the old one, if any, is removed after that — a failed install leaves the old version in use.
  The record `lib/aiml/aiml.assets.json` holds per name `source` (`kontor:<url>` or
  `path:<archive>`), `version` and `digest` (`sha256:` over the archive).  The same name with the
  same digest again is a no-op; another version or digest is the update.  A name that is also an
  installed tool's or that the venv's package table uses is no conflict: assets have a root of
  their own.
- **F68-127** **`asset list`** prints per package name, version, source, digest and the number
  of files; **`asset remove <name>`** deletes `<venv>/assets/<name>/` and its record.  Neither
  touches the cache.
- **F68-128** **Where the stores point.**  A compiled program (AOT and wasm32) registers its
  assets at start-up (spec/44_7 "Persistent paths"); a file of the venv's asset root is
  registered by its **absolute** path, a project file cwd-relative as before — the venv's files
  do not move with the working directory.  For `output_type: wasm`, `aiml build` preloads the
  venv's asset root under the same absolute path (`--preload-file=<root>@<root>`), next to the
  `asset_directories` (F68-88).
- **F68-129** **What this is not.**  No dependency between packages, no "latest" (the version is
  always named), no partial install, no per-project selection — every package of the venv is in
  every compile of that venv's compiler; a project that must not see one uses another venv.

Tests: `tests/venv/run.sh` §9 against `tests/venv/kontor_stub.aiml` (which also serves and takes
`/generic/<name>/<version>/<file>`): publish refused without a token, with an uppercase file, a
`-` in a directory, a double `_`, an unknown extension, a symbolic link, a duplicate stem within
the package and one against an installed package — each before anything is sent, the stub's
store unchanged; a published version refused a second time and replaced with `--overwrite`;
install from kontor and from a local archive into `<venv>/assets/<name>/`, the record, the same
digest a no-op, an update without a file of the old version left, a refused archive leaving the
old version in place, `list` and `remove`; and a program that finds a texture and a data asset
of a package by stem, through the JIT and as an AOT program run from another directory, without
`--asset-roots`.
