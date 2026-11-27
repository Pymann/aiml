# 62 - Pipelines: Matrix, Reuse, Parallelism [PROPOSAL]

## 1. Goal

A pipeline builds something, usually not once but across a set of configurations: architecture ×
compiler × variant × debug/release.  Two things are then wanted at the same time, and they pull in
opposite directions:

- **Permute hard.**  The interesting command is rarely one command; it is the same work over a
  parameter space, and the space is the point.
- **Do not repeat.**  A container build forms layers and therefore never installs the same package
  twice.  A pipeline usually forms nothing and is treated as non-repeatable, so it clones the same
  repository, installs the same package and creates the same directory again — per configuration,
  so the waste multiplies with the size of the matrix.

Those two combine better than they look: if the parameter bindings of a configuration are part of
the step key, then adding a compiler to the matrix re-runs only the new columns.  The matrix makes
reuse valuable, and reuse makes the matrix affordable.

### 1.1 What cannot be inferred

Whether a step would produce the same result is not decidable in general.  A step is a black box
whose output may depend on inputs that appear nowhere in its text: the current `HEAD` of a remote
branch, today's package index, the clock, a random number.  `git clone <url> <branch>` returns
different content on two days from the identical command line.

Container builders do not solve this; they *assert* it.  A key of "same instruction text + same
parent layer" is a claim about *declared* inputs, never a measurement of the *result*.  Hence
`RUN apt update` as a known trap and cache invalidation as a manual act.

So the question is not "how is equality proven" but **"how is the claim made honest"**.  Four
mechanisms, in decreasing strength:

1. **Typed parameters instead of a string environment** (§4).  A configuration is a set of bound
   values, declared with its domain.  They enter the key as values, not as hope.
2. **Split resolution from work** (§9).  A cheap probe turns a moving name into a fixed address:
   `git ls-remote` yields a commit id for a few kilobytes; `apt-get install --print-uris` yields
   version, URL and file hash without installing.  The probe runs every time; only the expensive
   part is reused.  The claim is then correct by construction, because the upstream system already
   guarantees it — a git commit id *is* a content hash.
3. **Enumerate the remaining inputs** (§10, F62-7).  Host values are listed explicitly; a key that
   ignores an input it depends on is blind by construction.
4. **Measure over time.**  A verify mode re-runs a step that hit and compares results, turning an
   unprovable assumption into an observation (cosmos `doc/spec/96` §96.5).

### 1.2 Why this is a language feature and not a data format

A first draft of this chapter was a YAML schema: a list of steps whose bodies were shell strings
and whose parameters were environment variables.  That is a model one could call a pipeline, and it
fails at exactly the things that matter here — one command per step, parameters in a flat untyped
host-global namespace, no permutation, no parallelism.  It is recorded here because the difference
is the justification for the chapter:

| | schema read at run time | language form |
|---|---|---|
| parameters | environment strings | declared, typed, scoped to the pipeline (§4) |
| a step | a record with a string body | a function: `args`, `return`, `body` — full aiml, result processed (§5) |
| permutation | written out by hand, or a generator | `params` domains expand to lanes at compile time (§4) |
| parallelism | left to the caller | lanes are independent by construction (§6) |
| purity claim | unchecked | checked per builtin; the one hole named and marked (§10.2/§10.3) |
| key | re-implemented per host | one canonical encoding, emitted by the compiler (§8) |

The matrix is the strongest of these.  Expanding a parameter space is a compile-time
transformation — the compiler knows the domains, so it can enumerate lanes, reject impossible
combinations and derive one key per lane.  A data format cannot do that; it can only be fed by
something that already did it.

## 2. Non-Goals

- No scheduling across machines, no placement.  Which node runs a lane is a host property
  (cosmos `doc/spec/90`/`91`).
- No layer stacking, no transfer between machines.  The local store (§12.2) is deliberately
  simple.  The **key** is shared ground and is defined here (§8, F62-21), because a local run and a
  cluster run are worthless to each other unless their keys agree.
- No container images.  Exporting a result as an OCI image, pushing it, or pulling one as a
  hermetic base is a separate concern with its own chapter (cosmos `doc/spec/96` §96.12 collects
  what was already found out).  It needs no change to this form — at most one more stage attribute,
  under F62-64.
- No guarantee of bit-identical output.  §16 states what is and is not promised.
- No implicit dependency inference between steps.  A step's inputs are what it declares.

## 3. Shape

```yaml
- pipeline:
    name:     <identifier>
    params:   [...]            # parameter space → lanes (§4)
    exclude:  [...]            # impossible combinations (§4.2)
    parallel: <n> | auto       # lane concurrency (§6)
    steps:    [...]            # a list of stages (§5.0)
```

A pipeline is a parameterized step graph plus the space it is instantiated over.  Without `params`
the space has exactly one lane, and the pipeline degenerates to a straight sequence — the simple
case stays simple.

## 4. Parameters and the Matrix

### 4.1 Declaration

```yaml
    params:
      - [arch,     string, [x86_64, arm64]]
      - [compiler, string, [gcc, clang]]
      - [mode,     string, [debug, release]]
      - [jobs,     i32,    [8]]
```

Each entry is `[name, type, domain]`.  The domain is a non-empty literal list of values of that
type; the cross product of all domains is the parameter space, and one point in it is a **lane**.
The example has 2 × 2 × 2 × 1 = 8 lanes.

Parameters are **typed values, not environment strings**.  Inside a step body a parameter is an
ordinary read-only local of its declared type, so `jobs` is an `i32` one can compute with, and a
typo in a parameter name is a compile error rather than an empty expansion at run time.

- **F62-1** A parameter domain is a non-empty literal list.  It is known at compile time, because
  the lane expansion is a compile-time transformation.
- **F62-2** Parameters are read-only within a step body.  A step that could change its own
  configuration would invalidate its own key.
- **F62-3** A parameter name must not collide with a host value name (§10, `host:`).

### 4.2 Excluding combinations

Not every point in a cross product exists:

```yaml
    exclude:
      - { arch: arm64, compiler: gcc }
      - { mode: debug, arch: arm64 }
```

An entry is a partial binding; every lane matching it is dropped.  Partial rather than complete
bindings, because the real statement is usually about two axes and should not have to be repeated
for every value of the others.

- **F62-4** An `exclude` key must name a declared parameter and its value must lie in that
  parameter's domain.  A typo would otherwise silently exclude nothing — the failure mode that
  makes matrix configuration untrustworthy elsewhere.
- **F62-5** Excluding every lane is an error.

### 4.3 What a lane is

A lane is a complete binding of all parameters.  It owns:

- one **workspace directory per stage** (F62-60), derived from the lane's and the stage's
  identity, so that concurrent lanes cannot see or corrupt each other,
- its own **step keys**, because the bindings a step declares are part of the key material (§8),
- its own ordered run through the stages and steps declared `per: lane`.

This is what makes reuse per column work: changing one axis changes the keys of the affected lanes
only, and the rest of the matrix is reused untouched.

## 5. Stages and Steps — a step is a function

A step is a `def` with pipeline attributes.  Nothing about its body, its arguments or its return
value is special:

```yaml
      - step:
          name:    <identifier>          # unique within the pipeline
          args:    [[<name>, <type>], ...]   # lane parameters this step uses (§5.2)
          return:  <type>                # any aiml type (§5.3)
          body:    [...]                 # aiml statements
          per:     lane | once           # default: lane
          needs:   [<step_name>, ...]    # default: the preceding step of the same `per`
          host:    [<name>, ...]         # host values used (§10, F62-7)
          cache:   pure | resolved | opaque   # default: opaque (§7)
          on_fail: lane | pipeline | continue # default: lane (§5.6)
          timeout: <ms>                  # whole step (§5.6)
          retry:   <n>                   # attempts, default 1 (§5.6)
          pause_before: true             # declared gate, waits for release (§5.9)
          resolve: [...]                 # required iff cache == resolved (§9)
```

- **F62-6** A step is a function definition.  `args`, `return` and `body` have exactly the meaning
  they have for `def` (`spec/09`), and everything a `def` accepts — `static_off`, `meta`, helper
  calls, loops, `try:`/`throw:` — is accepted here.  The pipeline form contributes the parameter
  space, the reuse classification, the key, the workspace and the parallelism; it contributes no
  way of writing code.

### 5.0 Stages — grouping, scope, and locality

Steps are grouped in stages.  A stage is not only a heading: it is the scope that holds what its
steps would otherwise repeat, and it is the unit of **locality**.

```yaml
    steps:
      - stage:
          name:    configure_and_build
          per:     lane | once            # inherited by its steps
          runs_on: "${arch}"              # one node for the whole stage (F62-60)
          args:    [[arch, string], [compiler, string], [mode, string]]
          host:    [CC_WRAPPER]
          cache:   pure
          on_fail: lane
          timeout: 3600000
          exec_defaults:                  # starting point for every sys.exe below
            cwd:     workspace
            env:     host_env
            capture: true
          steps:
            - step: { name: configure, ... }
            - step: { name: build, args: [[mode, string], [jobs, i32]], ... }
```

- **F62-57** A pipeline's `steps` is a list of stages, and a stage's `steps` is a list of steps.
  A bare `step` directly under the pipeline is shorthand for a stage of one.  The name is the same
  as the host's existing entity (cosmos `doc/spec/01` §1.5 `PipelineStage`), because it is the same
  thing — not a second grouping concept beside it.
- **F62-58** Attributes resolve **pipeline → stage → step, innermost wins**: `per`, `args`, `host`,
  `cache`, `on_fail`, `timeout`, `retry`, `exec_defaults`.  `params` exists only on the pipeline
  (it defines the matrix) and `runs_on` only on a stage (F62-60).
- **F62-58a** *(implemented 2026-11-27)*  What a step does not say it takes from its stage, and the
  stage from the pipeline: `per` (stage), `args` (stage; a `per: once` step takes none, F62-115),
  `host` (stage, pipeline), `cache` (stage, pipeline), `on_fail` and `retry` (stage), `timeout`
  (stage, pipeline — on the stage it stays the stage's limit as well, §18.1a) and `exec_defaults`
  (merged field by field over all three, F62-61).  `needs` on a stage is added to every step of
  it (F62-111).
- **F62-59** `args` on a stage is the set **in scope**.  A step that declares its own `args` selects
  a subset, which is the precise option and the one that pays (§5.2); a step that declares none
  inherits the stage's whole set, which is the convenient option and costs reuse.  The spec states
  the trade rather than picking for the author: inheriting is right while a stage's steps really do
  depend on everything, and wrong as soon as one of them does not.

#### The stage is the unit of locality

- **F62-60** One workspace per **(lane, stage)**, and one node per stage.  `runs_on` is therefore a
  stage attribute, and a step may **not** override it — steps in a stage share a workspace, and a
  workspace lives on one machine.
- **F62-13** *(amended)* A workspace belongs to a (lane, stage) pair rather than to a lane.  Two
  lanes never share a path, and neither do two stages of the same lane.

This makes the expensive boundary visible in the source.  Steps inside a stage hand their results
on for free — same machine, same directory.  A `needs` that crosses a **stage** boundary is where
an output tree may have to be transferred to another node (F62-48), and now one can see that by
reading the pipeline instead of by comparing resolved labels.

#### `exec_defaults`

- **F62-61** `exec_defaults` is an `ExeOptions` (F62-27) whose fields are the starting point for
  every `sys.exe` in scope: `decl: [o, ExeOptions]` yields the effective defaults rather than an
  empty model, and a call overrides field by field.  Resolution follows F62-58, so a pipeline sets
  what holds everywhere and a stage or step narrows it.
- **F62-61a** *In scope* is the step's body as written, nested blocks included — not a helper it
  calls, which is ordinary code with ordinary defaults.  Each field's value is source text: a name
  (`workspace`, `host_env`, an axis) or a literal; a path literal is quoted.  A field that is not
  one of `ExeOptions` is an error (F62-65).
- **F62-62** `exec_defaults` is how F62-30 and F62-31 are met in practice.  Requiring every call to
  pass `cwd: workspace` and `env: host_env` was boilerplate in every step of every pipeline — and
  boilerplate whose omission silently broke a `pure` claim rather than failing.  Declared once per
  stage, the correct thing is also the default thing.  The checks stay as they are: a `pure` step
  whose effective `env` is still inherited is an error, and now that error is almost always fixed
  in one place instead of twenty.
- **F62-63** `exec_defaults` is **not** separate key material.  Its effect reaches the key through
  the body: the defaults are part of the pipeline source, so they are covered by
  `step_source_hash` (§8).  Changing a stage's `exec_defaults` therefore changes the keys of its
  steps, which is correct — it changes what they run.

### 5.1 The body is plain aiml — no new statements

**This chapter adds no statement for running a command.**  Two earlier drafts did, and both were
wrong the same way: `run: "<shell line>"`, then `exec: [argv, options]`.  Each *swallowed the
result* — the step could not read stdout, could not look at stderr, could not branch on the exit
code.  That throws away what `spec/23` already provides and replaces it with less.

`sys.exe` returns a `process` carrying `stdout_buffer`, `stderr_buffer`, `returncode`, `wait()`,
`running()`, `read_line()`, `read_err_line()`, `eof()` and `kill()` (`spec/23` §5.2).  A step body
uses it directly:

```yaml
      - step:
          name: build
          needs: [configure]
          cache: pure
          args: [[arch, string], [compiler, string], [mode, string], [jobs, i32]]
          return: BuildReport
          body:
            - decl: [bdir, string, "build-${arch}-${compiler}-${mode}"]
            - decl: [argv, list<string>, ["cmake", "--build", bdir, "-j", "${jobs}"]]
            - decl: [opts, ExeOptions]
            - set: [opts.cwd, workspace]
            - set: [opts.env, host_env]
            - set: [opts.capture, true]
            - set: [opts.timeout, 1800000]
            - decl: [p, process, { call: [sys.exe, argv, opts] }]
            - decl: [rc, i32, { call: [p.wait] }]
            - if:
                condition: { neq: [rc, 0] }
                then:
                  - throw: ["BuildFailed", "${bdir}: rc=${rc}\n${p.stderr_buffer}"]
            - decl: [rep, BuildReport]
            - set: [rep.artifact, "${bdir}/service"]
            - set: [rep.warnings, { call: [count_warnings, p.stdout_buffer] }]
            - return: rep
```

A step can read a compiler's output and decide, stream a long log line by line, kill a hung child,
or start several children and wait for all of them — the same freedom a Groovy pipeline has, with
types.

- **F62-25** *Withdrawn.*  It described the output behaviour of the `exec:` statement, which no
  longer exists; `spec/23` §5.1 governs capture and redirection.

### 5.2 `args` — and why a subset is the point

`args` declares which lane parameters the step uses.  A **subset is allowed, and that is the
point**: the key carries only the declared bindings (§8), so a step that does not depend on an
axis is not re-run when that axis changes, and one result is shared across those lanes.

In the example of §11, `fetch_sources` declares no parameters at all, so it runs once for the whole
matrix; a `configure` that declares `[arch, compiler, mode]` but not `jobs` is unaffected by
changing `jobs`.  Declaring parameters a step does not use is therefore not harmless — it is how
reuse is lost.

Two arguments are appended by the compiler after the declared ones:

| Appended argument | Type | Meaning |
|---|---|---|
| `workspace` | `string` | absolute path of the lane's workspace directory (§4.3) |
| `host_env` | `map<string:string>` | exactly the names declared in `host:`, nothing else |

A host value is read from the environment of the process that runs the step — locally the `aiml`
process, under cosmos the executor — when the step is about to run, and it enters the key (§8).

- **F62-32** The five-argument limit (`spec/49` §15) applies to the **declared** `args`; the two
  appended ones do not count.  The limit keeps its meaning — a step depending on more than five
  axes is a design problem — while the lane context is not an axis.
- **F62-30** A lane's working directory is reached **only** by passing `cwd` to `sys.exe`, never by
  changing the process's own directory.  aiml offers no parent-side `chdir` — verified: the `chdir`
  in `process_runtime.cpp` happens in the child, and `posix_spawn` gets `addchdir_np`.  That is not
  a limitation to work around but the reason lanes may run in threads at all: a process-global
  working directory is shared by every thread, so a per-lane `chdir` would corrupt sibling lanes at
  random.  Should aiml ever gain one, using it inside a step is a defect, not a shortcut.
- **F62-31** A `pure` step must pass `env` explicitly, built from `host_env`.  `spec/23` inherits
  the parent environment by default, which would make every variable of the calling shell an
  undeclared input and a `pure` claim void.  Checked where the call's options are statically
  visible; a warning otherwise, because with `ExeOptions` as a value (F62-27) the field can be set
  anywhere.  In the declared form a `sys.exe` in a pure step's body is an error when neither its
  bag nor the effective `exec_defaults` (F62-61) sets `env`, and a warning when it takes its
  options from a value.
- **F62-26** *Superseded by F62-31*, which says the same thing but places the duty where it can be
  checked.

### 5.3 `return` — any aiml type, and it is cached

A step returns whatever its author declares: a model, a string, a list, nothing.  The value is the
step's result for everyone downstream.

- **F62-33** The return values of the steps named in `needs` are passed to a step as further
  arguments, before the declared parameters.  A `per: once` step depending on a `per: lane` step
  receives a `list<T>` over the lanes (F62-8); the other direction receives the single value.  This
  is how a pipeline carries data rather than only ordering.
- **F62-34** A step's return type must be serializable (`spec/29`).  A cache hit must reconstruct
  the value **without running the body**, so the value is stored in the cache beside the output
  tree.  A type that cannot be serialized cannot be returned from a cacheable step; `opaque` steps
  are unrestricted, since they never hit.
- **F62-35** Failure is a thrown exception (`spec/22`), not a return code.  An escaping exception
  fails the step and its lane, is recorded with the lane's bindings, and — like any other failure —
  is **not** cached: a step that threw has no result to reuse.  A reproducible *command* failure is
  a different thing: `returncode != 0` that the body chooses to return normally is a result and is
  cached (§7), which is what keeps a reliably broken build from being re-run forever.

### 5.4 `ExeOptions` — a built-in model, not a call-site literal

`spec/23` §5.1 calls the second argument of `sys.exe` an "option bag — a map literal written at the
call site …, **not a model**", and requires `bool` keys to be literals because they are resolved at
compile time.  For a pipeline that is the wrong shape, and the reason is this chapter's whole
point: **the exec description is the thing that should be reusable.**  A literal at a call site
cannot be built per lane, passed to a helper, stored beside its step or hashed as a value.

- **F62-27** The options of `sys.exe` are a built-in model `ExeOptions` — named after `sys.exe`
  and after the compiler struct that already holds them (`CompilerContext::ExeOptions`,
  `context.h`), so the name is not a new one.  It can be declared,
  built, passed and reused like any other model; a map literal at the call site stays valid as
  shorthand for constructing one.
- **F62-27a** *(implemented 2026-11-27)*  `ExeOptions` is declared by the compiler, as an ordinary
  `model` appended to any source that names it and does not declare it — so a bundle carries it
  like every other model (`spec/29` §3.3).  `sys.exe(argv, opts)` takes such a value; a literal
  bag on the same call overrides its fields one by one (`sys.exe(argv, opts, {capture: false})`).
  A model has no "unset", so the one reading of a default that a bag does not need: an **empty**
  `env` from a value means *inherit* (`spec/23`'s default), exactly as an `env` that is not
  given.  An empty environment on purpose is written as a literal bag.

| Field | Type | Meaning (`spec/23` §5.1) |
|---|---|---|
| `cwd` | `string` | working directory; `""` inherits, so a step passes `workspace` (F62-30) |
| `env` | `map<string:string>` | complete environment — replaces, does not extend; a `pure` step passes `host_env` (F62-31) |
| `shell` | `bool` | run `/bin/sh -c argv[0]`; see §10.3 for what it costs |
| `timeout` | `i32` | milliseconds from spawn |
| `capture` | `bool` | stdout/stderr to pipes → `stdout_buffer`, `stderr_buffer`, `read_line` |
| `stdout` | `string` | redirect stdout to a file |
| `stderr` | `string` | redirect stderr to a file |
| `detach` | `bool` | durable process (`spec/23` §5.4) |
| `status_file` | `string` | with `detach`: exit-status file |

Every field is `spec/23`'s field with `spec/23`'s meaning and `spec/23`'s default.  **This chapter
adds none and changes none** — the earlier draft's `allow_fail` and its "`cwd` defaults to the
workspace" belonged to the `exec:` statement and went with it.  A step that wants its lane
workspace says so (F62-30); a step that wants to tolerate a failure checks `returncode` and
returns `0`, which is ordinary code rather than a flag.

Durability comes along for free: `detach: true` with `status_file`, plus `sys.process_attach`
(`spec/23` §5.4), lets a long step survive a restart of whoever started it — exactly what an agent
needs, and nothing here has to invent it.

**This requires a change to `spec/23`, and the change is cheap — measured, not assumed.**
`spec/23` §5.1 calls the bag "not a model" and requires `shell`/`capture`/`detach` to be literals.
In the compiler those three are stored as C++ `bool` in `ctx.exeOpts` while the other six fields
are `llvm::Value*`, and at the call site they are emitted as `getInt32(o.shell ? 1 : 0)` — so
`aiml_process_spawn` already takes them as ordinary runtime integers.  The restriction is an
artefact of the compiler's storage, not of the runtime ABI, and lifting it protects nothing that
was being protected.

- **F62-29** `shell`, `capture` and `detach` become runtime values like the rest of
  `ExeOptions`.  The one consequence: the `capture: true` + `detach: true` incompatibility is a
  compile error today and moves into the runtime, which returns a `null` process and logs why.

### 5.5 `per:` — lane or once

`per: lane` is the default: the step runs once per lane.  `per: once` runs it a single time for the
whole pipeline, which is what fan-in and fan-out need:

```yaml
      - step:
      - step:
          name: fetch_sources        # fan-out: once, shared by all lanes
          per: once
          cache: resolved
          host: [REPO]
          args: []                   # no parameter → one result for the whole matrix
          return: string             # the commit that was checked out
          resolve: [...]             # §9
          body:
            - decl: [argv, list<string>, ["git", "clone", "--depth", "1", "${REPO}", "src"]]
            - decl: [opts, ExeOptions]
            - set: [opts.cwd, workspace]
            - decl: [p, process, { call: [sys.exe, argv, opts] }]
            - if:
                condition: { neq: [{ call: [p.wait] }, 0] }
                then: [{ throw: ["CloneFailed", "${REPO}"] }]
            - return: head_commit

      - step:
          name: build                # per lane, receives fetch_sources' return value
          per: lane
          needs: [fetch_sources]
          cache: pure
          args: [[arch, string], [mode, string]]
          return: BuildReport
          body: [...]

      - step:
          name: collect              # fan-in: once, receives list<BuildReport>
          per: once
          needs: [build]
          return: i32
          body: [...]
```

- **F62-8** A `per: once` step that `needs` a `per: lane` step receives the results of **all**
  lanes of that step — and depends on all of them: its key carries the keys of every lane of the
  need, in lane order, as one component (§8), so a matrix that gains or changes a lane makes it run
  again.  A `per: lane` step that `needs` a `per: once` step receives its single result.  Both
  directions are ordinary dependencies; no separate join syntax exists.
- **F62-9** `needs` may only name steps declared earlier, and step names are unique within the
  pipeline.  Together with F62-8 this makes the graph a DAG by construction, so no cycle check at
  run time is needed.

### 5.6 General attributes: `on_fail`, `timeout`, `retry`

What a failure destroys is a property of the step, not of the pipeline.  A failing
`fetch_sources` makes every lane pointless; a failing `build` in one lane says nothing about the
other five.  One pipeline-wide switch cannot express both, so the decision sits on the step:

```yaml
      - step:
          name:     build
          on_fail:  lane | pipeline | continue    # default: lane
          timeout:  <ms>                          # whole step, default: none
          retry:    <n>                           # attempts, default: 1
```

- **F62-39** `on_fail` decides the reach of a failure:
  - `lane` (default) — the lane stops; every other lane keeps running.  For a `per: once` step
    this means every lane that `needs` it is **skipped** — it never starts — while lanes that do
    not depend on it continue.  The reach follows the dependency graph instead of being guessed.
  - `pipeline` — scheduling stops, running lanes are allowed to finish, nothing new starts.  The
    right choice for a step whose failure invalidates the whole run.
  - `continue` — the failure is recorded, the lane goes on, and the lane carries a status distinct
    from both "clean" and "failed" (§5.7).  This is the explicit form of "tolerate", visible in the
    status rather than hidden in the body.
- **F62-40** `timeout` bounds the **whole step**, including every process it starts and the time
  between them.  On expiry the step's children are killed — every process the body started with
  `sys.exe` and all their descendants (found through `/proc/<pid>/task/*/children`), a detached
  job with its monitor — and the step counts as failed.  A process the body starts **after** the
  expiry is killed as soon as it is started, so a body that loops over `sys.exe` comes to an end.
  A child the body has already waited for is no longer killed — its pid may belong to another
  process by then — and a descendant that outlived its parent (reparented to init) is out of
  reach: the tree is followed from the children the body still holds.
  This is not the same as `ExeOptions.timeout`, which bounds one child: a step that starts forty
  compilers needs a bound on the step.
- **F62-40a** What cannot be killed is the body's own code: it is JIT code in the lane's thread, and
  no thread of a process can be stopped from outside without leaving its locks and its memory in
  an unknown state.  A body that waits for its children returns once they are killed; a body that
  computes without a child runs until it returns and then fails as expired.  Both are logged with
  what happened — how many processes were killed, or that there were none to kill.
- **F62-41** `retry: n` runs the body up to `n` times, stopping at the first attempt that does not
  throw.  `retry > 1` together with `cache: pure` is an **error**: a pure step depends only on
  hashed inputs, so a second attempt must fail identically, and asking for one states a
  contradiction.  With `resolved` and `opaque` it is allowed, because their failures can be
  transient — a lost connection is the usual case.  Only the successful attempt is recorded (§7);
  the failed attempts appear in the log with their attempt number.

### 5.7 Status: a six-value enum, and how a body sets it

A step's status is not a success flag.  `success | unstable | failure` cannot tell apart three
things that matter here: whether the step ran, whether it worked, and whether the answer came from
the cache.  Reuse is the point of this chapter, so a status that cannot show it hides what one
most wants to see.

- **F62-42** `StepStatus` is an enum with exactly six values, named after the three additive
  primaries and their pairwise mixtures: `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`.
  **What each value means is deliberately not fixed in this draft** (§16 item 6).  Six and the
  mixture names are fixed because the structure is: three facets that combine pairwise, which is
  what a wall display shows and what an aggregate has to be computed from.

#### Setting it from a body

A step's status is assigned automatically from how the step ended, and a body can override it.
No new syntax: the pipeline form puts three functions in scope of every step body, exactly as it
supplies `workspace` and `host_env` as arguments (§5.2).

| In scope | Signature | Effect |
|---|---|---|
| `set_status` | `(s: StepStatus)` | sets this step's outcome, overriding the automatic one |
| `fail_lane` | `(msg: string)` | ends this lane; other lanes keep running |
| `fail_pipeline` | `(msg: string)` | ends the whole run; nothing further is scheduled |

```yaml
          body:
            - decl: [p, process, { call: [sys.exe, argv, opts] }]
            - decl: [rc, i32, { call: [p.wait] }]
            - if:
                condition: { eq: [rc, 2] }                 # tolerable: warnings only
                then:
                  - call: [set_status, StepStatus.yellow]
                  - return: rep
            - if:
                condition: { neq: [rc, 0] }
                then:
                  - call: [fail_lane, "build rc=${rc}: ${p.stderr_buffer}"]
            - if:
                condition: { eq: [{ call: [io.exists, "src/BROKEN"] }, true] }
                then:
                  - call: [fail_pipeline, "source tree unusable — every lane would be pointless"]
```

- **F62-45** `set_status`, `fail_lane` and `fail_pipeline` are generated functions in scope of a
  step body and of a `resolve:` body.  They are functions and not statements because the body is
  plain aiml (F62-6) and a call needs no new grammar; they are generated rather than builtins
  because the pipeline adds no runtime (F62-23).
- **F62-46** `fail_lane` and `fail_pipeline` do not return.  They are the explicit form of what
  `on_fail` (F62-39) decides for an uncaught exception: the declaration sets the default reach, a
  call overrides it at the point where the body knows better.  A step therefore never has to encode
  "how far this breaks" into an exception name.
- **F62-43** A **lane** and a **pipeline** each carry one `StepStatus`, aggregated from their
  steps by a **total precedence order** over the six values — not by additive mixing.  Mixing is
  the right mnemonic for one step and the wrong rule for an aggregate: all three facets together
  would be a seventh value, and "did anything fail" would stop being answerable at a glance.  The
  order itself follows from the meanings and is open with them (§16 item 6); that it must be total
  and must not be mixing is not open.
- **F62-44** The six values are **outcomes**, and a step has one only once it is finished.  Whether
  it is waiting or running is a separate, orthogonal state — `pending | running | done` — and must
  not be folded into the same enum.  Folding them is what forces a list like
  `pending | running | succeeded | failed | skipped`, in which `running` and `failed` are not
  alternatives of the same kind and for which no aggregation rule can be stated.  Two enums, read
  together: a step is `running`, and when it is `done` it has its colour.

- **F62-47** **Magenta means a version incompatibility** (decided 2026-11-26).  It is the first of
  the six values whose meaning is fixed (§16 item 6): a step is magenta when something it built or
  ran was made against a version outside its pin — `aiml build` says so with a
  `version-mismatch` line (aiml spec/68 F68-118), and a host that sees the line in a step's output
  marks the step magenta.  A body can set it itself with `set_status(StepStatus.magenta)`.
  Magenta is not a failure: the step ran and its result stands; what it says is "this worked
  against something it was not pinned to" — the signal the person who maintains the pin needs,
  and one a red step would drown.
- **F62-48** **A lane and a pipeline show every colour that occurred, not one.**  The aggregate of
  F62-43 still exists and still answers "did anything fail" at a glance, but it is no longer all a
  lane or pipeline carries: next to it stands the **set** of the colours its steps ended in.  A
  pipeline that is green overall and had one magenta step is shown green **and** magenta, so a
  version incompatibility is never hidden by a precedence order that ranks it below success or
  failure.  The set is a set, not a count and not a mixture: it says which signals are present,
  F62-43 says which one decides.

### 5.8 Where a stage runs — and what travels there

`runs_on` names a node label and sits on the **stage**, never on a step (F62-60): the steps of a
stage share one workspace, and a workspace lives on one machine.  It may interpolate parameters,
which is the case that matters:

```yaml
      - stage:
          name: build
          args: [[arch, string], [mode, string]]
          runs_on: "${arch}"        # arm64 lanes build on an arm64 node
          steps: [...]
```

An empty `runs_on` means "wherever the run was started" — locally that is this machine, which is
why a local JIT run (§12) needs no node concept at all.

- **F62-47** `runs_on` is a label expression on a stage, resolved per lane.  Which node carries
  which label, and how one is chosen when several match, is the host's business (cosmos
  `doc/spec/90`/`91`) — the language says only *which* label a stage wants.
- **F62-48** A lane may therefore span nodes: `configure` on the build host, `test` on the target.
  When two stages linked by `needs` resolve to different nodes, the input's workspace has to exist
  on the target node, which is the content-addressed transfer of cosmos `doc/spec/96` §96.2.  So a
  differing `runs_on` between stages is what makes that transfer **mandatory** rather than an
  optimization — and
  it is the reason it belongs in the design now and not later.
- **F62-49** `runs_on` is **not** key material.  Where a step ran must not change its key,
  otherwise the same work done on two nodes would never be shared and the cache would fragment per
  machine, which defeats its purpose.  The sharp edge of that rule: if a property of the node does
  affect the result — the architecture it builds for, the toolchain it carries — it must appear as
  a **parameter** (§4) or a `host:` value (§10), because only those are in the key.  The example
  above is correct precisely because `arch` is a declared parameter and `runs_on: "${arch}"` merely
  follows it; writing `runs_on: "builder-7"` and hoping the architecture comes along would produce
  a key that cannot tell two architectures apart.

#### Does the body travel to the node?

Yes, and the reason is not convenience.  A step body calls `io.*` on its lane workspace, inspects
files the previous step produced, and starts processes.  All of that is on the node.  If the body
ran on the controller and only its `sys.exe` calls were forwarded — which is what a thin agent
would suggest, and what cosmos does today with a shell line plus `cwd` — then `io.exists` in that
same body would silently ask the **wrong machine**.  A split like that does not fail loudly; it
answers questions about the controller's filesystem while reading like it asks about the node's.

So the whole body executes on the node, and what travels is its **source** together with its
arguments:

- **F62-50** What is sent to a node is a **`Code` bundle** (`sys.bundle`, `spec/29`/`spec/29_2`),
  executed there with `sys.exec` — not a source file handed to a command-line compiler.  A bundle
  carries its dependencies transitively (`spec/30_3`), so it is self-contained by construction;
  "which files does the step need" stops being a question the pipeline has to answer.  It is also
  an ordinary serializable value, so it travels over the existing RPC path with nothing new
  invented for it.
- **F62-51** A node that runs steps therefore needs **libaiml with a JIT context**, not the `aiml`
  binary.  The compiler is already inside the runtime library — measured: `libaiml.so` exports
  LLVM symbols, `aiml_exec_code` and `aiml_set_active_context`.  What an AOT binary lacks is an
  *active* context: `jitRun()` sets it, and an `exodus build` binary never calls that, so
  `sys.exec` returns `-2` there today (`spec/29_2` §11).  The missing piece is the isolated JIT
  (`new_context = 1`), which `spec/29_2` §3.2 already foresees as Phase 2.  **This is the one
  blocker for distributed execution**, and it is a gap in an existing mechanism rather than a new
  subsystem.
Sending a bundle does **not** widen the trust boundary in kind: a host that accepts a step already
accepts arbitrary command execution from whoever defines the pipeline (cosmos `doc/spec/90`).  It
does widen it in degree, because an aiml body reaches more builtins than a shell line, and the
host's answer to that is isolation, not a smaller payload.

### 5.9 Pausing and resuming

A run must be stoppable and continuable at every step, from outside — an operator decision, not a
property of the code.  There is exactly one place where that is possible, and saying so is the
whole of this section.

- **F62-53** A run pauses **only at a step boundary**.  A body is ordinary aiml with live child
  processes; there is no way to suspend it mid-flight and nothing honest to pretend.  Stopping its
  children with a signal would leave half-written files in the lane workspace, and that workspace
  is what becomes the step's output tree — a poisoned tree would be hashed, recorded, and handed
  out as a cache hit later.  The boundary is also the only point where the state is well defined:
  the workspace is a complete tree, and the step's key is known.
- **F62-54** `pause_before: true` on a step is a **declared** gate: the lane reaches the step and
  waits for a release.  This is the approval step — "someone has to look before this deploys" —
  and it belongs in the pipeline source because it is part of the process, not of the operator's
  mood.
- **F62-55** A pause requested while a step is running takes effect **after** that step.  To stop
  something that is already running one aborts it, which is a different act with a different
  record: an abort leaves no output tree and nothing cached, a pause leaves a complete one.
  Conflating them is how a build system ends up caching rubble.
- **F62-56** Resuming needs no state of its own.  What a resume continues from is the set of
  `(step, lane)` keys already recorded together with their output trees (§8) — which is exactly
  the cache.  Three things follow, and none of them has to be built:
  - a resume after a **controller restart** is the same operation as any resume,
  - a lane may resume on a **different node** than it started on, because its state is
    content-addressed rather than tied to a machine (F62-48),
  - a resume is indistinguishable from a second run of the same pipeline that happens to hit the
    cache for everything already done.
- **F62-44** *(amended)* The lifecycle state of a lane or run is `pending | running | paused |
  done`, still separate from the outcome (§5.7).  `paused` is a lifecycle state and never one of
  the six outcome values: a paused lane has no outcome yet, which is precisely why it can continue.

A paused run keeps its lane workspaces, so pausing costs disk for as long as it lasts.  That is a
deliberate price — the alternative is to drop the workspaces and re-run from the cache on resume,
which is correct but throws away the uncached work of the step that was about to run.

## 6. Parallelism

Lanes do not share a workspace and do not depend on one another, so they are parallel by
construction — the matrix is where the concurrency comes from, not an added feature.

```yaml
    parallel: 8        # at most 8 lanes at a time; `auto` = one per core
```

Within a lane steps run in order; across lanes they run concurrently up to the limit.  A `per: once`
step is a barrier for whatever depends on it.

- **F62-10** `parallel` bounds concurrent **lanes**, not processes.  A step that itself starts
  parallel work (`cmake --build -j`) is not accounted for; the author sets `parallel` and `jobs`
  against each other.  Pretending to control total load would be a promise the pipeline cannot
  keep.
- **F62-11** Generated code uses `spawn:`/`join` from `spec/26`.  Native OS threads, so lanes are
  genuinely parallel.
- **F62-12** A failing lane does not abort the others by default; every lane's status is reported
  (§5.7) and the pipeline's status is the aggregate (F62-43).  The default is to see the whole
  matrix, because with a matrix the *pattern* of failures is the information — one broken compiler
  looks different from one broken architecture.  Which failures reach further than their own lane
  is decided per step by `on_fail` (F62-39), not by a pipeline-wide switch.
- **F62-13** Each lane gets its own workspace directory, named from the lane's identity.  Two lanes
  never share a path.

### 6.1 The hazard to prove, not assume

A step body starts processes from several threads at once.  `fork`/`exec` from a threaded process is a
known hazard: the child inherits locks and file descriptors of a process that is mid-flight
elsewhere.  cosmos already hit the descriptor half of this (`FD_CLOEXEC` on an inherited listener,
memory of the SSH work).

- **F62-14** Concurrent process start must be demonstrated under load before `parallel > 1` is
  enabled — by measurement, not by argument.  If it does not hold, the fallback is a single
  spawner: one thread starts all processes, the lanes wait for their results.  That keeps the
  concurrency of the *work* while serializing only the few microseconds of process creation.

#### The measurement (2026-10-11)

`tests/test_concurrent_spawn_hazard.aiml` is this measurement: **eight threads each start twelve
processes**, i.e. 96 starts with maximum overlap, each child with its **own** pipes
(`capture: true`).  What is checked is not speed but that every start succeeds,
every exit code arrives correctly and **no output wanders into a foreign thread** — the
descriptor half of the hazard that cosmos already hit with the inherited listener.

Three runs: `96/96` succeeded, `0` failed, `0` swapped, ~12 ms.

- **F62-101** `parallel > 1` is therefore **cleared**, with two requirements that follow from the
  measurement and not from an argument:
  - A step whose output is read runs with `capture: true`.  Without it the child inherits
    the parent process's stdout, and several lanes write into the same stream — in the first
    measurement attempt this looked like `96` swapped outputs and was in truth the missing pipe.
    The difference matters: the hazard is not that pipes fail, but that there are none
    at all.
  - The measurement applies to `sys.exe` from threads.  It says **nothing** about whether several
    isolated compilations (`sys.exec`, §13) may run at the same time — that is LLVM state,
    not process state, and needs a measurement of its own (F62-99).

## 7. Reuse Classes

| `cache:` | Meaning | Examples | Reusable |
|---|---|---|---|
| `pure` | output depends only on hashed inputs | create a directory, copy, compile, unpack a pinned archive | yes |
| `resolved` | impure, but a cheap probe yields a fixed address | `git clone`, `apt install`, download | yes — the probe runs every time |
| `opaque` | no probe exists (clock, live API, randomness) | `curl <api>`, `date` | **no** |

**The default is `opaque`.**  A container builder assumes everything is pure and hands the
consequences to the user; here it is the opposite.  Nothing is reused until something has been
asserted, so a wrong reuse is always a claim the author made, never one the system made on their
behalf.

## 8. Step Key

One key per (step, lane).  `sha256` over canonical material, each component length-prefixed as
`<byte-length>:<value>`, concatenated without a separator:

```
<step_source_hash> <class> <lane_bindings> <input_keys> <host_values> <resolve_output>
```

The **step key** is the lowercase hex sha256 of that material; `input_keys` are the step keys of
the needs, and the local store names an entry by it (F62-24).  A list component is its count
followed by its entries, each length-prefixed; `<class>` is the reuse class as its number
(`opaque` 0, `resolved` 1, `pure` 2 — F62-15).

- `step_source_hash` — hash of the step's body **as written**, before parameter substitution.
- `lane_bindings` — the bindings of the parameters the step **declared** in `args` (§5.2), sorted
  as `name=value`, with the count included.  Not the whole lane: a step that does not declare an
  axis is reused across it.
- `input_keys` — the keys of the steps named in `needs`, sorted.  A changed input changes the key
  transitively, which is what makes a chain of steps behave like a stack of layers.
- `host_values` — the names in `host:` with their values, sorted, as `name=value`, count included.
- `resolve_output` — the probe's output (§9), empty for `pure` and `opaque`.

Length prefixing rather than a separator character, because any separator can occur inside a value:
with newline-joined material, one variable whose value contains a newline and two separate
variables produce identical material — one cache entry for two different configurations.  Sorting
makes declaration order irrelevant; including each count makes *omitting* an entry a change as
well.

- **F62-36** `cwd` is no longer a key component.  It used to be a step attribute; it now lives in
  `ExeOptions` inside the body (§5.4) and is therefore covered by `step_source_hash` — and since
  the body is hashed **as written**, it contains the identifier `workspace`, never a machine path.
- **F62-37** A cache entry holds the step's **return value** beside its output tree (F62-34), so a
  hit reproduces both.  Without the value a reused step would be invisible to its own successors.
- **F62-168** **`outputs:` — the output tree.**  A step lists what it leaves for its successors:
  `outputs: [build/app, "build/**/*.so", reports/]` — paths and globs (`*`, `?`, `[..]`, `**` for any
  number of directories) relative to its (lane, stage) workspace; a directory stands for every
  file below it.  After a successful run each pattern must match at least one file, else the step
  **fails** naming the pattern (a missing output is a broken step, not an empty tree).  Each file
  goes into the store's `blobs/` under the sha256 of its content, once, and the entry records
  `(sha256, mode, path)` per file beside the result (F62-37).  A **hit** writes the files back into
  the workspace before the successors run, so a reused step leaves the same tree a run would; a
  blob that is gone makes the hit a miss, and the step runs.  The output tree is a result, **not
  key material**: it is what the key's inputs produced (§8).  A symbolic link is stored as the file
  it points to.  Without `outputs:` a step's files stay where they are and nothing is stored.
- **F62-15** The reuse class is part of the key material.  `pure` and `resolved` with identical
  bodies are not the same step: the class is part of the claim, not a switch on the side.

The host's `ActionCache` implements this encoding (cosmos `doc/spec/92` §92.2).

### 8.1 Extending stage and step — the one question a new attribute must answer

The stage/step form is meant to grow: an `image:` for a hermetic base, an `exports:` for artefacts,
resource limits, a cache scope.  Growth is additive and does not disturb what exists, with one
exception that is **not** optional.

- **F62-64** Every new stage or step attribute must declare whether it is **key material** (§8).
  There are only three correct answers, and the third is the trap:
  1. *Covered by `step_source_hash`* — the attribute only shapes code that is already hashed as
     written.  `exec_defaults` is of this kind (F62-63).
  2. *Its own key component* — the attribute changes what the step produces without appearing in
     its source.  It must be added to the material of §8, length-prefixed like the rest.
  3. *Deliberately not key material* — the attribute has no effect on the result.  `runs_on` is
     the only one so far (F62-49), and it needed the argument that anything about the node which
     *does* affect the result must be declared as a parameter instead.

  The trap is answering nothing.  An `image:` that is forgotten in the key would let two stages
  with different base images share one entry, and the cache would hand out results built against
  the wrong system — silently, and with full confidence.  A forgotten attribute is therefore worse
  than a missing one.

- **F62-65** Unknown attributes on a stage or step are an **error**, never ignored.  A typo in an
  attribute name must not degrade quietly to the default, because the defaults here are the
  dangerous direction: `cache` falls back to `opaque` (harmless) but `on_fail` to `lane` and
  `args` to the whole stage set, both of which change behaviour without saying so.

## 9. `resolve:` — the Probe

`resolve:` is a second function body on the step, evaluated **before** the key is computed, on
every execution, never reused.  It returns a `string`: the resolved address.

- **F62-28** `resolve:` is a body returning `string`, with the same arguments as the step itself.
  Its return value becomes `resolve_output` in the key material (§8).  A body rather than a
  statement list with a special terminator, so that a probe may compute, branch, log and call
  helpers before it commits to an address — and so that nothing new has to be learned to write one.

Its contract:

- It asks for an address; it does not fetch content.
- It writes nothing into the workspace.
- A thrown exception fails the step.  It never degrades to "run anyway", because a failed
  resolution means the key is unknown, and an unknown key must not hit — otherwise "I could not
  check" turns silently into "nothing has changed".
- **F62-89** The probe runs in the **caller's** context, the body in the **isolated** context.
  Measured while writing `tests/test_pipeline_resolve.aiml`: a global variable that the
  probe increments takes effect; the same assignment in the body reports `Undefined variable`, because a bundle
  carries code and no process state (`spec/29_2` §13).  That is intended and useful — the
  probe may read the host's state (configuration, network, working copy), the body may not.
  What a body **does** see of the host is stated in `spec/29_2` §22: built-in functions yes,
  host functions as a bundled **copy**, global variables only as a freshly initialised
  declaration — and files, database and process rights **shared**.  The separation is
  lexical and not a sandbox (F29_2-22b); a pipeline whose source text comes from outside is
  therefore the same class of error as a remote shell.
  Whoever wants to count or measure in the body uses the runtime counters (`ran_count`/`reused_count`,
  §13.1) or the observer (§13.2) — both live outside the isolated context.

```yaml
      - step:
          name: deps
          cache: resolved
          host: [PKG]
          args: []
          return: i32
          resolve:
            - decl: [argv, list<string>,
                     ["apt-get", "install", "--print-uris", "-y", "${PKG}"]]
            - decl: [opts, ExeOptions]
            - set: [opts.capture, true]
            - decl: [p, process, { call: [sys.exe, argv, opts] }]
            - if:
                condition: { neq: [{ call: [p.wait] }, 0] }
                then: [{ throw: ["ResolveFailed", "${PKG}"] }]
            - return: p.stdout_buffer
          body:
            - decl: [argv, list<string>, ["apt-get", "install", "-y", "${PKG}"]]
            - decl: [opts, ExeOptions]
            - set: [opts.env, host_env]
            - decl: [p, process, { call: [sys.exe, argv, opts] }]
            - return: { call: [p.wait] }
```

The probe's stdout carries URL, size and hash of every `.deb` that would be fetched, and the
resolved version with it — measured on Ubuntu 24.04:
`'http://…/sl_5.02-1_amd64.deb' sl_5.02-1_amd64.deb 12708 MD5Sum:a9d3404…`.  So `apt install` on
two days with a new package in between yields two different keys, which is the case a container
builder gets wrong.

Note what argv costs: several packages are several argv elements, not one space-separated string,
because there is no shell to split them.  A list-valued input is built in the body
(`argv.extend`) like any other list.  Where a pipe or a glob is genuinely wanted, `shell: true` is
available — and in a `resolve:` it costs nothing in soundness, because a probe is never reused.

### 9.1 `cache:` and `resolve:` on the stage — the weakest step decides

A stage is the host's `PipelineStage` (F62-57), and a host reuses **stages**: cosmos keeps one
layer per stage and skips a stage whose key it has seen (cosmos `doc/spec/96` §96.6).  So the
reuse class has to be readable on the stage, and it has to mean the same thing whether the stage
carries one `run:` line or a list of steps.

- **F62-159** `cache:` resolves pipeline → stage → step, innermost wins (F62-58).  A step without
  its own `cache:` takes the stage's, a stage without one takes the pipeline's, and the default
  stays `opaque` (§7).
- **F62-160** `resolve:` may stand on a stage.  Its form follows the stage:
  - on a stage with `run:` it is a **shell line** (a string), because the stage's command is one.
    It is the probe of §9 with the same contract: it runs before the key, every time, on the
    executor; its trimmed stdout is the address; a non-zero exit fails the stage, it never
    degrades to "run anyway".
  - on a stage with `steps:` it is a **body** (a list) and the default probe for every step in it
    that is `resolved` and declares no `resolve:` of its own (innermost wins).
  `resolve:` is not inherited from the pipeline and is an unknown key there (F62-65): a probe
  belongs to what it probes.
- **F62-161 The weakest step decides.**  The class of a stage with `steps:` is the **weakest**
  effective class of its steps, ordered `opaque` < `resolved` < `pure`.  One `opaque` step makes
  the whole stage `opaque`; one `resolved` step in an otherwise `pure` stage makes it `resolved`.
  A stage with `run:` has its own effective class (F62-159).
- **F62-162** A stage is reused as a whole only when its class is not `opaque`.  Its key covers
  the keys of all its steps, so the probe output of every `resolved` step moves it.  The steps
  before an `opaque` step stay reusable **one by one** (§8): their keys do not depend on what
  comes after them.  What is lost is only skipping the stage as a unit.
- **F62-163** F62-16 holds for the stage as well, with the effective class: a `resolved` stage
  with `run:` needs a `resolve:` line, a `pure` one must not carry one.  A string `resolve:` on a
  stage with `steps:`, or a body on a stage with `run:`, is an error — not a guess at what was
  meant.
- **F62-164** The readers return the **resolved** values (§20.3): `stg.cache` is the class after
  F62-159 and F62-161, `stg.resolve` the probe line of a `run:` stage ("" otherwise).  A host
  computes neither itself; otherwise it would be a second parser (§20.1).

```yaml
    cache: pure                         # pipeline default (F62-159)
    steps:
      - stage:
          name: quellen
          cache: resolved
          resolve: "git ls-remote https://example.org/app.git refs/heads/main"
          run: "git clone --depth 1 https://example.org/app.git /ws/app"
      - stage:
          name: bauen                   # inherits pure from the pipeline
          run: "make -C /ws/app -j8"
      - stage:
          name: pruefen                 # stg.cache == opaque: `zeit` is opaque (F62-161)
          steps:
            - step: { name: build, return: i32, body: [...] }          # pure, reusable alone
            - step: { name: zeit, cache: opaque, return: i32, body: [...] }
```

## 10. Static Checks

Extends `spec/49_static_analysis.md`.  Errors unless stated otherwise.  F62-1…F62-5 (parameters),
F62-8/F62-9 (graph) and F62-13 (workspaces) are checks as well and are stated where they belong.

### 10.1 Structure

- **F62-16** `cache: resolved` requires a non-empty `resolve:`; `cache: pure` must not carry one.
  The same holds for a stage with `run:` (F62-163).
- **F62-7** Every `${NAME}` interpolated in a step or `resolve:` body must resolve to a declared
  parameter or to a name listed in `host:`.  This closes the blind-key hole of §1.1 item 3 at
  compile time, and it is the check that makes an untyped environment unnecessary.
- **F62-17** `host:` carries names, never `name=value`.  A value in the source would be a secret in
  the source.

### 10.2 Purity

Purity is a property of a **function**, not of a namespace: `yaml.parse` is pure while
`yaml.parse_file` reads a file; `path.join` is pure while `path.home` depends on the host.

- **F62-18** Every builtin carries a purity classification in the registry.  A `pure` body may call
  only builtins classified pure (plus `io` under F62-19/F62-20 and `log`).  An unclassified builtin
  counts as impure — nothing becomes reusable by omission.

Namespace defaults, overridable per function:

| Default | Namespaces | Why |
|---|---|---|
| pure | `math`, `hash`, `enums`, `compress`, `cascade` | computation over their arguments |
| impure | `time` | clock |
| impure | `net`, `http`, `ws`, `wss`, `tls`, `ssh`, `mirror` | network |
| impure | `sys` | environment, subprocesses, randomness |
| impure | `db`, `storage` | external mutable state |
| impure | `ai` | not reproducible |
| impure | `input`, `ui`, `engine`, `gl`, `gltf`, `audio` | interactive |
| mixed | `path`, `yaml` | per-function, see below |
| mixed | `vocab`, `tensor` | process-global state; §17 item 3 |

- `path`: pure are `from_string`, `join`, `safe_join`, `empty`, `ends_with`; impure are `home`,
  `cwd`, `temp_dir`, `current_exe`, `config_dir` — each answers a question about *this host*.
- `yaml`: `parse`, `dump` and the accessors are pure; `parse_file` falls under the `io` rule.
- `log` is always permitted: log output is not part of the result.
- **F62-112** `step` is **pure** (the namespace from §13.5e/§5.7).  `step.arg` and `step.host`
  read exactly what is in the key anyway — the **declared** axes and host values
  (§8); calling them impure would turn key completeness against itself.
  `step.workspace` is derived from lane and stage and is therefore fixed per lane (F62-107).
  `step.set_status`, `step.fail_lane` and `step.fail_pipeline` say something about the **outcome**,
  not about the result, and a failure is not recorded (F62-92).  Noticed
  while rewriting the declared form: its generated bodies read their axes with `step.arg`,
  and a pure step would otherwise have become impossible.

`io` cannot be banned — writing files is what a build step is for.  Instead:

- **F62-19** A *constant* absolute path in `io.*` outside the lane workspace is an error in a `pure`
  body.  Reading `/etc/hostname` makes the result host-dependent.
- **F62-20** A *computed* path in `io.*` in a `pure` body is a **warning**.  It cannot be decided
  statically, and claiming otherwise would be dishonest.

### 10.3 The one hole the compiler cannot close

There is no `kind: shell` any more.  An earlier draft had a step body that was either aiml or
shell; with the body being a function (§5) the distinction disappeared — the body is always aiml,
and a shell appears only as `shell: true` on an `ExeOptions` passed to `sys.exe`.  That is an
improvement in honesty, because it puts the hole exactly where it is instead of at the level of a
whole step.

The hole: `sys.exe` runs a program, and the compiler cannot see inside a program.  `sys` is
classified impure (F62-18), yet starting a program is the normal content of a build step, so the
ban cannot simply apply.

- **F62-22** A `pure` step may call `sys.exe` — the single impure call allowed — and then requires
  `assert_pure: true` on the step.  The assertion is the record that a human vouched for what the
  program does, since nothing else can.  Everything else about `sys` (`sys.env`, randomness)
  remains banned in a `pure` body: those have aiml-level alternatives, so there is no reason to
  make an exception for them.
- **F62-38** `shell: true` in a `pure` step is a **warning** even with `assert_pure`.  It adds word
  splitting, globbing, aliases and `$HOME` between the declaration and what runs — more invisible
  inputs for no gain, since argv is available.  It is not an error, because a pipe is sometimes
  genuinely the shortest correct thing.

What a step that spawns programs still gets is weaker than a proof and much stronger than a
container builder: the probe runs every time (§9), parameters and host values are declared and
checked (F62-7/F62-17), only the declared axes enter the key (§5.2), the default is no reuse (§7),
and verify mode reports divergence over time (cosmos `doc/spec/96` §96.5).

## 11. Example

```yaml
- model:
    name: BuildReport
    fields:
      - [artifact, string]
      - [warnings, i32]

- pipeline:
    name: build_matrix
    params:
      - [arch,     string, [x86_64, arm64]]
      - [compiler, string, [gcc, clang]]
      - [mode,     string, [debug, release]]
      - [jobs,     i32,    [8]]
    exclude:
      - { arch: arm64, compiler: gcc }
    parallel: 4
    steps:
      - stage:
          name: sources
          per: once                      # one checkout for the whole matrix
          cache: resolved
          host: [REPO_URL]
          exec_defaults: { cwd: workspace, capture: true }
          steps:
            - step:
                name: clone
                return: string           # the commit that was checked out
                resolve:
                  - decl: [argv, list<string>, ["git", "ls-remote", "${REPO_URL}", "refs/heads/main"]]
                  - decl: [proc, process, { call: [sys.exe, argv, { capture: true }] }]
                  - if:
                      condition: { neq: [{ call: [proc.wait] }, 0] }
                      then: [{ call: [step.fail_pipeline, "ls-remote failed"] }]
                  - return: { call: [proc.stdout_buffer] }
                body:
                  - decl: [argv, list<string>, ["git", "clone", "--depth", "1", "${REPO_URL}", "src"]]
                  - decl: [proc, process, { call: [sys.exe, argv] }]
                  - if:
                      condition: { neq: [{ call: [proc.wait] }, 0] }
                      then: [{ call: [step.fail_pipeline, { call: [proc.stderr_buffer] }] }]
                  - return: { call: [read_head, workspace] }

      - stage:
          name: compile
          needs: [clone]                 # every step of the stage gets ${clone}
          agent: "${arch}"               # the matrix axis picks the node (§18.5)
          args: [arch, compiler, mode]
          cache: pure
          on_fail: lane                  # one broken column must not stop the rest
          timeout: 3600000
          host: [CC_WRAPPER, PATH]
          exec_defaults: { cwd: workspace, env: host_env, capture: true }
          steps:
            - step:
                name: configure
                assert_pure: true        # a program runs; someone vouches (F62-22)
                return: string           # the build directory
                body:
                  - decl: [bdir, string, "build-${arch}-${compiler}-${mode}"]
                  - decl: [argv, list<string>, ["cmake", "-S", "../sources/src", "-B", bdir,
                                                "-DCMAKE_C_COMPILER=${compiler}", "-DARCH=${arch}",
                                                "-DCMAKE_BUILD_TYPE=${mode}"]]
                  - decl: [proc, process, { call: [sys.exe, argv] }]
                  - if:
                      condition: { neq: [{ call: [proc.wait] }, 0] }
                      then: [{ call: [step.fail_lane, { call: [proc.stderr_buffer] }] }]
                  - return: bdir

            - step:
                name: build
                needs: [configure]       # ${configure} is the build directory
                args: [mode, jobs]       # narrower than the stage
                assert_pure: true
                return: BuildReport
                outputs: ["build-*/service"]
                body:
                  - decl: [argv, list<string>, ["cmake", "--build", configure, "-j", "${jobs}"]]
                  - decl: [opts, ExeOptions]           # = the stage's exec_defaults
                  - set: [opts.timeout, 1800000]
                  - decl: [proc, process, { call: [sys.exe, argv, opts] }]
                  - decl: [code, i32, { call: [proc.wait] }]
                  - if:
                      condition: { eq: [code, 2] }
                      then: [{ call: [step.set_status, StepStatus.yellow] }]
                  - if:
                      condition: { gt: [code, 2] }
                      then: [{ call: [step.fail_lane, "rc=${code}"] }]
                  - decl: [rep, BuildReport]
                  - set: [rep.artifact, "${configure}/service"]
                  - set: [rep.warnings, { call: [count_warnings, { call: [proc.stdout_buffer] }] }]
                  - return: rep

      - stage:
          name: publish
          per: once
          needs: [build]                 # per: once over a per-lane step: list<BuildReport>
          host: [STATUS_URL]
          steps:
            - step:
                name: report
                pause_before: true       # someone looks before this goes out (§5.9)
                return: i32
                body:
                  - decl: [total, i32, 0]
                  - decl: [idx, i32, 0]
                  - while:
                      condition: { lt: [idx, build.len] }
                      body:
                        - decl: [rep, BuildReport, { at: [build, idx] }]
                        - set: [total, { +: [total, rep.warnings] }]
                        - set: [idx, { +: [idx, 1] }]
                  - call: [log.info, "matrix done, ${total} warnings"]
                  - decl: [argv, list<string>, ["curl", "-fsS", "-X", "POST", "${STATUS_URL}"]]
                  - decl: [proc, process, { call: [sys.exe, argv] }]
                  - return: { call: [proc.wait] }
```
Six lanes (8 minus the two excluded).  Four things are worth reading off this example:

**The stage carries what the steps would repeat.**  `exec_defaults` gives every `sys.exe` below it
`cwd: workspace` and `env: host_env`, so `decl: [opts, ExeOptions]` in `build` already holds them and
only `timeout` has to be set.  `configure` passes no options at all and still runs in the right
directory with the right environment — the correct thing is the default thing (F62-62).

**Reuse is finer than the matrix.**  The `compile` stage has `[arch, compiler, mode]` in scope;
`build` narrows to `[mode, jobs]`, because what it needs of `arch` and `compiler` is already inside
the build directory it receives from `configure`, and that value reaches the key through
`input_keys` (§8).  Raising `jobs`
therefore re-runs `build` and leaves every `configure` result in place.

**The expensive boundary is visible.**  `sources` and `publish` run wherever the run started;
`compile` runs on a node chosen by `${arch}`.  The two `needs` edges that cross a stage boundary
are exactly the two places where an output tree may have to travel (F62-48) — readable in the
source rather than inferred from resolved labels.

**Failure reach and status are explicit.**  `compile` declares `on_fail: lane`, so a broken column
leaves the other five running; `build` treats a return code of 2 as a tolerable outcome with its own status
and anything worse as a lane failure; `clone` calls `fail_pipeline`, because a matrix without
sources is pointless.  `publish` waits for a release before it runs (§5.9).

## 12. Local Execution (JIT)

```
aiml build_matrix.aiml
```

A pipeline runs locally — no server, no agent, no network.  This is not a convenience bolted on; it
decides where the executor lives.

### 12.1 Where the executor lives

aiml ships no standard library and has no package chapter: everything useful is either a builtin
namespace or an `import:` of a path.  Shipping a runner as library source is therefore not
available.  Three candidates remain:

| Option | Consequence |
|---|---|
| a `pipeline` builtin namespace (C++) | key, cache and store re-implemented in the runtime, duplicating what a host has in aiml source |
| `pipeline:` **generates aiml code** | no new runtime surface; the generated body uses `hash`, `io`, `db`, `sys.exe` and `spawn`, which all exist |
| the user supplies a runner by `import:` | a pipeline file is then not self-contained — rejected |

- **F62-23** `- pipeline:` **generates** a function `run_<name>` returning `i32` (0 = every lane
  succeeded).  Lane expansion happens at compile time; the generated body uses only existing
  builtins.  Same approach as the generated unit-test harness (`spec/59`).

Without a `main` the compiler generates one that calls the single declared pipeline, so
`aiml build_matrix.aiml` runs it.  With several pipelines, or other work to do, the author writes
`main` and calls `run_<name>`.  A host system calls the same generated function, so a local run and
a remote run execute the same code rather than two implementations kept in agreement.

### 12.2 Where the local store lives

- **F62-24** The local store sits in `store/` of the workspace root — `.aiml_pipeline/store/` of the
  **working directory** unless `workspace_root` says otherwise — beside the workspaces (§4.3), with
  `blobs/` for content (the output trees, F62-168) and `kv/` for `step_key → (status, result, output manifest)`, one file per key named by the sha256 of the
  key material.  An entry written since 2026-11-27 is `aiml-step-entry-2` (with the manifest of
  F62-168); `aiml-step-entry-1` entries, which have none, are still read.  `AIML_PIPELINE_STORE` names another directory; `pipe.set_store(dir)` does so for one
  pipeline value (a test that counts hits isolates itself with it).  Project-local rather than in
  the user's cache directory, because a pipeline belongs to a project and a cache one cannot find is
  a cache one cannot delete.  *"Beside the pipeline file"*, as this said until 2026-11-27, is not
  what a run can know: the compiled code does not carry its source's path, and an AOT binary runs
  far from it.
- **F62-24a** **What a later process finds.**  A cacheable step's successful result (F62-92) is
  written to the store when the step ends, and looked up there when the step's key is not in the
  pipeline value's memory — so a second `aiml build_matrix.aiml` reuses what the first one built,
  which is the point of §1 locally.  An entry is written next to its name and renamed into place,
  so a run that dies halfway leaves no torn entry; an entry that cannot be read (truncated, another
  format) is a **miss** with a warning, never an error and never a hit.  `opaque` steps are never
  written (§7).  The store is never pruned by a run; deleting the directory is the reset.

### 12.3 The same key locally and remotely

A local run is pointless if its keys differ from the cluster's — then there are two islands and
nothing is shared.  What was built on a laptop should be reusable by the cluster and vice versa.

- **F62-21** No key component may contain a host-specific value.  This largely follows from the
  shape rather than needing enforcement: the body is hashed as written (§8), while `workspace` and
  `host_env` are **arguments**, so a path appears in the key as an identifier and never as a value.
  What remains is the author's own doing and is visible because it is declared: a `host:` value
  holding a machine-specific path makes the key machine-specific.  Hostname, user and home
  directory never enter the key.

### 12.4 What this unblocks

Workspace content — create a directory, clone, compile — needs no root and no namespace, so the
whole mechanism including verify mode is provable locally in JIT mode, with no agent, no SSH and no
isolation work.  Only *system* layers (`apt install`, which writes `/usr` and runs dpkg triggers)
need privileges.  Hosts should order the work accordingly (cosmos `doc/spec/96` §96.8).

## 13. Runtime mapping — what a run *is* while it runs

A pipeline is **declared**, not assembled.  §4 expands the matrix at compile time, §10 checks the
declaration, §8 hashes the step's written source.  All three stop working the moment a pipeline can
be put together from parts at run time, so this chapter does not offer that (§13.3).

What it does offer is the other half: while a run happens, it **is** something a host can read and
steer.

### 13.1 The models the generated code fills

`run_<name>` (F62-23) does not only execute — it maintains a value per lane and per step, and hands
it out.

```yaml
- model:
    name: Lane
    fields:
      - [bindings,  map<string:string>]   # arch=arm64, compiler=clang, …
      - [workspace, string]               # per (lane, stage), F62-60
      - [state,     i32]                  # pending | running | paused | done (§5.9)
      - [status,    i32]                  # StepStatus, valid from done (§5.7)

- model:
    name: StepRun
    fields:
      - [name,        string]
      - [lane,        i32]                # index into the lane list
      - [step_key,    string]             # §8
      - [output_tree, string]             # "" = nothing produced
      - [reused,      i32]                # 1 = hit, body did not run
      - [state,       i32]
      - [status,      i32]
      - [exit_code,   i32]
      - [started_ms,  i64]
      - [ended_ms,    i64]
```

- **F62-76** These models are the **only** interface between a running run and
  its host.  A host reads them; it does not build them.  What it does with them — write them into a database,
  push them over Mirror, draw them in a scene — is its own business (cosmos
  `doc/spec/96`/`97`/`98`).
- **F62-77** They are **serialisable** (`spec/29`), for the same reason as a return value
  (F62-34): a run that takes place distributed must be able to hand its state over the wire.

### 13.2 How the host gets them

- **F62-78** `run_<name>` takes an optional **observer**: a function called with the current
  `StepRun` on every state change of a step.

  ```yaml
  - def:
      name: on_step
      args: [[run, ref<StepRun>]]          # ref<>, not StepRun and not ptr<>
      body: [ { call: [push_to_ui, run] } ]

  - decl: [rc, i32, { call: [pipe.run, on_step] }]
  ```

  **`ref<StepRun>`, and that is not a matter of style.**  Measured: `StepRun` as a value reads garbage (the
  runtime passes a pointer, the signature expects a value), and `ptr<StepRun>` **crashes**
  — `ptr<T>` means ownership, so the receiver frees, on leaving the scope, a struct
  that belongs to the runtime (`__libc_free` on the way back).  `ref<T>` is the borrow
  (`spec/61` §6.2) and the only correct choice.

  An observer instead of a return value, because the host has to react **during** the run —
  pause, display, abort.  A result at the end would come too late for all of that.
- **F62-79** Without an observer the pipeline runs just the same; the local JIT run (§12) needs none.
  The observer is an extension, not a prerequisite.
- **F62-80** The observer must **not** change the run.  Technically it gets a
  `ref<StepRun>` (see above — a value reads garbage, a `ptr<>` leads to a foreign free),
  but the value lives **only for the duration of the call**: it is rebuilt for each event.  Whoever
  writes into it changes nothing — and that is exactly what is intended.  Control goes through the host side
  (pause/resume/abort, cosmos `doc/spec/96` §96.11), not through a model.

### 13.3 Two forms: declared and composed

There are **two** ways to a pipeline, and they differ not in the result but
in when the structure is fixed.

| | **declared** (`- pipeline:`) | **composed** (`pipeline` type, §13.5) |
|---|---|---|
| Structure | source text | built at runtime |
| Matrix | expanded at compile time (§4) | expanded at the start of the run |
| Structure checks (§10.1) | compile error | error at start, **before** any step runs |
| Body checks (§10.2, purity) | compile error | **also a compile error** — the body is a `Code` bundle |
| Key (§8) | hash of the written body | hash of the bundle content (`AimlCode.id`) |

**The point where the line runs is the body, not the structure.**  A
composed step gets its body as `Code` (`sys.bundle`), i.e. as compiled,
checked source text with a content address.  This is why the two properties that matter
are preserved: the purity check applies (it checks the body) and the cache key is
content-derived (it hashes the body).

What is really lost is one thing: **the structure check moves from build time to the
start of the run.**  A typo in a parameter name, a `resolved` without a probe, an `exclude`
on an axis that does not exist — the declared way reports these at compile time, the
composed way at start.  Earlier would be better; "before any step runs" is the
next-best place and much better than in the middle of the run.

- **F62-81** Both forms lead to the **same** execution and the same runtime models
  (§13.1).  There is no second executor and no laxer path; a composed run
  is cached, observed and controlled exactly like a declared one.
- **F62-84** A composed pipeline is **fully checked** at start and fails
  before any step runs.  Partially executed runs from a faulty structure are
  ruled out — they would leave cache entries behind for a pipeline that never existed in that form.

### 13.5 The types `pipeline`, `stage`, `step`

```yaml
- decl: [bld, step, { call: [step.new] }]
- call: [bld.set_name, "build"]
- call: [bld.set_body, { call: [sys.bundle, "do_build", { context: true }] }]   # Code
- call: [bld.set_cache, CacheClass.pure]
- call: [bld.use_arg, "mode"]                              # only declared axes (§5.2)

- decl: [cmp, stage, { call: [stage.new] }]
- call: [cmp.set_name, "compile"]
- call: [cmp.set_runs_on, "${arch}"]
- call: [cmp.add_step, bld]

- decl: [pipe, pipeline, { call: [pipeline.new] }]
- call: [pipe.set_name, "generated"]
- call: [pipe.add_param, "arch", arch_values]              # list<string> at runtime
- call: [pipe.add_stage, cmp]
- decl: [rc, i32, { call: [pipe.run] }]                    # optional: pipe.run(on_step)
```

#### 13.5a `on_fail`, `timeout`, `retry` and the status in the composed form

```yaml
- call: [bld.set_on_fail, OnFail.pipeline]      # §5.6, default: OnFail.lane
- call: [bld.set_timeout, 60000]                # ms, whole step (F62-40); 0 = none
- call: [bld.set_retry, 3]                      # attempts (F62-41), default 1
- decl: [st, i32, { call: [bld.status] }]       # StepStatus, valid from done (§13.1)
- decl: [lst, i32, { call: [pipe.status] }]     # aggregated over the lanes (F62-43)
```

- **F62-90** The three functions from §5.7 (`set_status`, `fail_lane`, `fail_pipeline`) are generated in the
  **declared** form (F62-45), whereas in the **composed** form they are built-in
  functions in the namespace `step`: `step.set_status`, `step.fail_lane`, `step.fail_pipeline`.  They
  write to a runtime-side slot for the *currently running step* — thread-local,
  because lanes may run in threads (§6).
  The namespace is not a cosmetic flaw but forced, and that was noticed during implementation:
  in this form a step body is an **ordinary function** that
  `sys.bundle` merely collects *in addition* — so it is also compiled
  in the ordinary context.  A name that exists "only in the step body" fails exactly there with
  `Unknown function`.  Hence a name that resolves everywhere, but whose **effect** is bound to a
  running step: without a running step the call is an error and not a
  silent no-op.  That the declared form can keep the bare names (F62-45) remains
  correct — there the compiler generates the body including the preamble and knows that it is a body.
- **F62-94** The third value of `on_fail` is called `OnFail.proceed` in the composed form,
  not `OnFail.continue`: `continue` is a keyword in aiml (loops), and an
  enum member cannot have that name.  In the **declared** form it stays the attribute value
  `continue` from §5.6 — there it is text in a mapping and collides with nothing.  The same
  concept, two spellings, because one place is grammar and the other is not.
- **F62-91** *(F62-64 obligation: key material?)*  `on_fail`, `timeout` and `retry` are
  **not** key material — all three describe what happens on a **failure**, and
  only a **successful** attempt is ever recorded (§7, F62-41).  Two steps that
  differ only in these fields deliver the same result on success; separating them
  would cost reuse with nothing in return.  Cross-check: `timeout` is not key material
  because an expiry makes the step **fail** (F62-40) and a
  failed step records nothing — not because the time would be unimportant.
- **F62-92** `fail_lane` and `fail_pipeline` prevent recording; a bare `set_status`
  does **not** prevent it.  Reason: which colours mean "failure" is open (§16 item 6) —
  binding recording to an open meaning would mean making a decision that is
  explicitly not made here.  Whoever does not want a run recorded says so
  with `fail_lane`, and that means it independently of any colour interpretation.
#### 13.5g `per:` and `needs:` — order, failure reach, results

```yaml
- call: [clone.set_per, Per.once]            # §5.5: once for the whole matrix
- call: [build.add_need, "clone"]            # §5.1/F62-9: only earlier steps
```

The separation is not convenience but a **measured limit**: `aiml_exec_code` returns
a **status**, not the value of the body (`tests/test_bundle_exec_semantics.aiml`:
`return 7` in the bundle yields `rc=0`).  In the composed form the return value of a
step is therefore **not retrievable**, and F62-33 (the values of the `needs` as further arguments) is
not unfinished but ruled out by today's ABI.

- **F62-113** In the composed form `needs` means **order, failure reach and
  result access** (§13.5h) — but not argument passing: the return value of a body
  reaches the successor not as an argument but via the result channel, and whoever expects it as an
  argument gets nothing.  Besides that, the **workspace** (§4.3) remains the way for
  everything that is not a string: files.
- **F62-114** *(F62-64 obligation: key material?)*  `needs` **is** key material, namely
  via `input_keys` (§8): the key of a step contains the keys of its
  `needs`.  A change further up therefore propagates transitively downwards — that is what makes a chain
  of steps behave like a stack of layers in the first place.  Cross-check: if it were
  left out, a successor would hit its old entry although its predecessor ran anew, and
  the workspace beneath it would be a different one.
- **F62-115** *(F62-64 obligation)*  `per` is **not** key material, but it limits what MAY be in the
  key: a `per: once` step has **no** lane bindings, because there is one
  result for the whole matrix.  A `per: once` with declared axes (`use_arg`) is
  therefore an **error** and does not "take the first lane" — the latter would be a result that
  depends on the order of the lanes.
- **F62-116** At every `per: once` step the execution becomes a **barrier**: all lanes
  reach it, it runs exactly once, after that the lanes continue.  Between two barriers
  lane parallelism (§6) remains — the barrier is the only place where there is waiting,
  because it is the only place where something shared comes into being.
#### 13.5h The result channel — data flow without changing the JIT ABI

F62-113 recorded that the return value of a body is not retrievable: the generated
entry point calls the start function and discards its result, and `aiml_exec_code` returns
a **status**.  But it does **not** follow that there can be no data flow — only that it
does not have to go through the return value.  A body runs in the same process as the runtime;
it can **publish** its result, just as it already sets its status (F62-90).

```yaml
          body:
            - call: [step.set_result, head_commit]      # publish
            - return: 0
          # in the successor that has `clone` in needs:
            - decl: [commit, string, { call: [step.need_result, "clone"] }]
```

- **F62-118** Results are **strings** — the serialised form (F62-34).  `step.set_result`
  also takes a number and writes it in decimal (64-bit values as 64-bit); what is read is always
  a string.  Reason: the value must be **recordable** next to the key, and what is recorded is
  serialised.  In the composed form a model goes through the channel via
  `sys.serialize`/`sys.deserialize` (`spec/29`) written by the author; the declared form does
  that itself (F62-121).
- **F62-118a** `step.need_results(name)` → `list<string>`: in a `per: once` step, the results of a
  `per: lane` need, **one per lane in lane order** (F62-8).  A lane that produced none — it failed
  or was skipped — contributes no element.  `step.need_result` on such a need is an error (there
  is no single value), and so is `step.need_results` on a need that has one.
- **F62-119** `step.need_result(name)` is an **error** if `name` is not in the `needs` of the
  calling step — not an empty string.  The same reasoning as for F62-106:
  a result that affects the body without being in the key via `needs` (`input_keys`,
  F62-114) would be a **blind key**.  The channel is thus not only convenient but opened exactly
  as far as the key reaches.
- **F62-120** A hit **restores** the result without calling the body (F62-37).
  The cache therefore holds `(status, result)` and not just the status.  Without that, a
  reused step would be invisible to its own successors — they would get nothing and
  would have to run anew, which would cancel the reuse exactly where it is worth
  the most.
- **F62-121** In the **declared** form the compiler publishes: from
  `return: <type>` a wrapper body is made that calls the written body and puts its result into the
  channel — a string or an integer as it is, **any other type serialised** (`sys.serialize`,
  `spec/29`).  The author therefore still writes `- return: rep` (§5.3) and does not need to know
  anything about `set_result`.  Conversely, the results of the `needs` appear as **local names
  equal to the step name** (like the axes, F62-109), **with the type the needed step declares**:
  `${clone}` is the string `clone` returned, `build.warnings` a field of the `BuildReport` `build`
  returned (read back with `sys.deserialize`).  A `per: once` step that needs a `per: lane` step
  gets a `list<T>` under that name, one element per lane in lane order (F62-8, F62-118a).  F62-33 is
  thus **fulfilled** in the declared form, for every type `spec/29` serialises.  A name that is
  both an axis and a `needs` would be bound twice in the body and is an **error**, not a
  precedence rule.
- **F62-121a** The body travels as a bundle (`sys.bundle`, §5.8), so the types it uses must travel
  with it.  A **model** the body declares or returns is part of the bundle like a class
  (`spec/29` §3.3) — until 2026-11-27 it was not, and a step returning a model failed at run time
  with "Unknown Type".

- **F62-117** If a `per: once` step fails with `on_fail: lane`, exactly the steps that
  (transitively) need it are **skipped** — they do not start.  Steps that do not
  need it continue to run.  The reach thus follows the graph and not a guess
  (F62-39).  A skipped step has not failed: it has no outcome and is
  not recorded.

#### 13.5e How a body sees its lane

In the declared form a body gets the declared axes as **arguments**, plus
`workspace` and `host_env` (§5.2).  In the composed form the body is a bundle that is
called without arguments — so far `use_arg` only declares what goes into the **key**.  That would
make the matrix ineffective: every lane would execute the same body without knowing which lane
it is.  Noticed while implementing §10.

- **F62-105** The body reads its lane via the namespace `step`:

  | Call | Result |
  |---|---|
  | `step.arg("arch")` | the value of this axis in this lane |
  | `step.workspace()` | absolute path of the workspace of this (lane, stage) (§4.3) |
  | `step.host("PKG")` | the host value of this name, otherwise empty |

  It is read from a thread-local slot that the runtime sets before the body — the same
  construction as `step.set_status` (F62-90) and for the same reason: a bundle carries no
  state of the caller (F62-89).
- **F62-106** `step.arg(name)` is an **error** if the axis was not declared with `use_arg`
  — not an empty value.  Reason: an axis that the body reads but does not
  declare would be a **blind key**: the value affects the result but is not
  in the key, and two different lanes would share an entry.  That is exactly the hole that
  §1.1 describes; an empty value would hide it.  The counterpart of F62-7, only at
  runtime instead of in the source text, because in this form only the runtime knows the declaration.
- **F62-107** *(F62-64 obligation: key material?)*  The workspace path is **not**
  key material, although the body reads it.  It contains lane and stage, which are already in the key via the
  bindings (§8), plus the root directory — including that
  would give the same project different keys at another location, and no hit
  would survive a move.  That the path CAN appear in the result (for instance in a log line) is
  a reason for `opaque`, not for a new key field.

#### 13.5d How §10 reaches the composed form

§10 checks **statically**, and in the composed form `set_cache(CacheClass.pure)` is only fixed
at **runtime**.  That would restrict the purity check (§10.2) to the declared form
— i.e. remove it precisely where pipelines come out of a database or a generator.

- **F62-102** Purity is declared at **bundling** time, not when the class is set:
  `sys.bundle("do_build", { context: true, pure: true })`.  That is the place where the
  compiler has the body in hand, and it is the same place where
  completeness is already checked (`spec/29_2` §18).  `pure: true` triggers exactly the checks from
  §10.2; without the flag nobody checks anything, and `set_cache(pure)` is then a claim
  without backing.
- **F62-103** A step with `cache: pure` whose body was **not** bundled with `pure: true`
  is a **warning case** at runtime, not an error.  Reason: the runtime cannot verify the
  claim (the body is a bundle), and aborting a run because a
  *check* is missing would make the pipeline hostage to a flag that the author may have
  deliberately left out.  The warning says exactly that: here `pure` holds unproven.
- **F62-104** `assert_pure` (F62-22) belongs in the same place for the same reason:
  `sys.bundle("do_build", { context: true, pure: true, assert_pure: true })`.  Without
  `assert_pure`, a `sys.exe` in the body of a `pure: true` bundle is an **error** — not
  because `sys.exe` would be bad, but because the signature is missing that a human has to provide
  (the compiler cannot look into a program).

#### 13.5c `parallel` in the composed form

```yaml
- call: [pipe.set_parallel, 8]      # at most 8 lanes at once; 0 = auto (one core per lane)
```

- **F62-98** `parallel` limits concurrent **lanes** (F62-10) and is **not**
  key material (F62-64 obligation): it decides how many lanes run at the same time, not
  what a lane produces.  Two runs of the same pipeline with `parallel: 1` and `parallel: 8`
  must produce the same keys — otherwise concurrency itself would be an
  input, and a hit would depend on how fast the machine was.
- **F62-99** In this form the step body is a bundle that is **compiled** at runtime
  (§13).  The runtime serialises exactly this section — the lanes run in parallel, the
  compilation of a body does not.  That is the fallback variant from F62-14, applied to the
  compiler instead of to process start.

  **Measured (2026-10-12, `tests/test_concurrent_isolated_exec.aiml`):** 24 isolated
  compilations, once from **8 threads** and once **single-threaded**, the same work:

  | Form | Duration (3 runs) |
  |---|---|
  | 8 threads | 1040 / 1026 / 1032 ms |
  | single-threaded | 958 / 967 / 964 ms |

  Concurrency brings **nothing** — it is even slightly slower, and 240 compilations over
  10 runs went through without errors.  Throughput is **independent** of the thread count
  (~40 ms per compilation), although there is no lock of its own in the isolated path: somewhere in
  LLVM there is serialisation anyway.
  **Consequence:** the lock stays.  It costs measurably nothing and takes an entire class of errors
  out of play — that is a better deal than a concurrency that brings no gain.
  The question is therefore **answered**, not open: whoever raises it again must first show that
  throughput depends on the thread count at all.

  **⚠️ Addendum 2026-10-17 — the measurement above is no longer complete.** The same test
  (`tests/test_concurrent_isolated_exec.aiml`) **crashed with SIGSEGV** in the full corpus run of 2026-10-16
  (`--clean` full build, the only failure outside the two
  spec-before-code labs).  After that it ran without errors in the next full run and in **six**
  consecutive individual runs.  The bug is therefore **sporadic and load-dependent** — the
  10 runs of 2026-10-12 did not hit it, which is to be expected at this hit rate.

  That does not change the **consequence**, it reinforces it: the lock stays, and now not only
  because concurrency brings nothing, but because the isolated path is demonstrably
  not safe under load.  What changes is the sentence "went through without errors" — it holds for those
  10 runs, not as a statement about the path.  Whoever raises the question again must also explain this
  crash.  Cross-reference: `spec/65` F65-25 (one process per user) relies on this.
- **F62-100** The observer (§13.2) is called **serialised**, even when lanes run in
  parallel.  It is user-written aiml code in the caller's context (F62-89); calling it from eight
  threads at once would mean making every observer thread-safe without warning.
  The order of the calls is then not the source order — that is unavoidable with parallel
  lanes and is stated here so that nobody relies on it.

#### 13.5b Pausing and resuming in the composed form

```yaml
- call: [dep.set_pause_before, 1]            # §5.9, declared gate (F62-54)
- call: [pipe.request_pause]                 # from outside, takes effect AFTER the step (F62-55)
- call: [pipe.release, "deploy"]             # release a declared gate
- call: [pipe.resume]                        # continue; the rest comes from the cache (F62-56)
- decl: [lc, i32, { call: [pipe.lifecycle] }]  # Lifecycle.pending|running|paused|done
```

- **F62-95** `pipeline.run()` returns `-6` if the run was **paused**.  Not `0`: a
  paused run is not finished, and `0` would mean "through".  Not the colour of a step:
  `paused` is lifecycle, not an outcome (F62-44 extended).  After a `resume`, a further
  `run()` is literally the same as a second run of the same pipeline — the already
  recorded steps hit, the paused one runs.  F62-56 is therefore not
  implemented but **not needed**: resuming needs no state of its own.
- **F62-96** *(F62-64 obligation: key material?)*  `pause_before` is **not**
  key material.  It decides **when** a step runs, not **what** it produces —
  the same step with and without a gate delivers the same result, and putting a gate into the key
  would mean counting every release as a new result.  Cross-check: if a gate influenced the
  content (for instance because someone changes the workspace while waiting),
  it would not be the gate that is key material but the change — and that falls under §7
  (`opaque`), not under this field.
- **F62-97** In this form, pausing from outside happens from the **observer** (§13.2): it runs
  per (step, lane) in the caller's context (F62-89), i.e. exactly where a user interface
  or a service sits.  `request_pause` from a step body works as well, because the slot
  is on the pipeline and not in the isolated context.

- **F62-93** The precedence order from F62-43 is in **one** place in the implementation, as a
  table (`kStatusPrecedence`, `pipeline_runtime.cpp`), and is explicitly **provisional** until
  §16 item 6 is decided.  Tests therefore only check what is already decided: that the
  order is **total**, that it aggregates instead of mixing (F62-43), and that the colour of a
  step reaches the lane — no individual colour meaning.

Methods instead of field assignment, and that is not a matter of taste: built-in types in aiml are
**opaque handles** (`Code`, `process`, `path`).  `set: [bld.name, …]` would presuppose a struct member
— and a struct cannot carry the variable structure (step list, axis list).  The
value is a handle on something that grows.

- **F62-85** `step.body` is a **`Code`**, not a string and not a function pointer.  A string
  would be unchecked text; a function pointer would have no content address.  `Code` is both:
  compiled and hashed.
- **F62-86** `use_arg(name)` corresponds to `args:` of the declared form (§5.2) — it says which
  axes this step uses, and **only these** go into its key.  Without it, the step inherits
  the axes of its stage; that is the convenient and the more expensive variant, just as there.
- **F62-87** `add_step` and `add_stage` **copy** the passed value in.  A `step` changed
  afterwards no longer affects the stage it is already in — otherwise a
  pipeline could change its shape after its keys were formed.  Whoever wants two similar steps
  builds two and adds both.
- **F62-88** `pipeline.run()` is the same in both forms: the same lane expansion, the same
  key formation, the same observers.  The declared way builds the same structure at
  compile time instead of receiving it at runtime.

### 13.5f The declared form is sugar on top of the composed one

The declared form (§5) is rewritten into the composed form **before** compilation.
It is therefore not a second implementation but a notation — and that is exactly what
F62-23 demands (the compiler generates the executor) without getting two executors that drift
apart (§16 item 4).

```yaml
- pipeline:
    name: demo
    params: { arch: [x86_64, arm64] }
    stages:
      - stage:
          name: compile
          steps:
            - step: { name: build, cache: pure, args: [arch], return: i32, body: [...] }
```

becomes three ordinary declarations: a function for the body, optionally one for the
probe, and a function `demo()` that composes `pipeline`/`stage`/`step` and returns the pipeline.

- **F62-108** A declared pipeline `name: demo` produces a function `demo()` with
  return type `pipeline`.  The name must be a valid aiml identifier.  It is therefore only
  built when someone calls it — the declaration itself executes nothing, and `demo().run()`
  is the same as a hand-written composed pipeline.
- **F62-109** The declared axes appear in the body as **local variables** of the same
  name, generated from `step.arg(...)` (§13.5e); plus `workspace` and one name each from `host:`.
  That is why one writes `${arch}` in the body and means the axis.
- **F62-110** **F62-7 therefore needs no check pass of its own**: a `${NAME}` that is neither a
  declared axis nor a `host:` name is an unknown name in the generated body and
  therefore a **compile error** — the error that F62-7 demands falls out of the rewrite.
  That is better than a second check that can disagree with the first: whoever
  forgets an axis gets it reported at the place where it is used.
  **Measured** (`tests/pipeline_declared_neg/undeclared_axis.aiml`): the error comes, but the
  *message* is imprecise — `Unknown Type: 'mode'` instead of "unknown name", because
  name resolution falls into type resolution in this situation.  That is a separate defect
  (it confuses name and type) and not a property of F62-7; it should be fixed without the
  statement here depending on it.
- **F62-111** What the rewrite does **not** cover is **rejected** during rewriting, never ignored —
  a silently discarded field would be a pipeline that runs differently from what it says and
  reports green while doing so.  That is `runs_on` (renamed `agent:`, §18.5) and `outputs:` **on
  a stage** (an output belongs to the step that writes it; on a stage it would not say which step
  that is).  Carried since 2026-11-27: `per:` and `needs:` **on a stage** — `per` is inherited by
  the stage's steps, innermost wins (F62-58); `needs` is added to every step of the stage, so a
  stage names once what all its steps depend on (only steps of **earlier** stages, F62-9) — and
  `outputs:` on a step (F62-168).  The data flow of the `needs` values is carried too (F62-121).

### 13.6 Generating pipelines as source text — the third way

Besides the declared (§13.3) and the composed form (§13.5) there is a third way:
**generate source text** and compile it normally.  It pays off where the structure comes from data
and one still wants the checks at **build time** — the composed way moves them
to the start of the run (§13.3), this one does not.

```yaml
- def:
    name: build_pipeline_source
    args: [[rows, list<StageRow>]]
    return: { type: string, lifetime: producer }
    body:
      - decl: [src, string, "- pipeline:\n    name: generated\n    steps:\n"]
      - decl: [idx, i32, 0]
      - while:
          condition: { lt: [idx, rows.len] }
          body:
            - decl: [row, StageRow, { at: [rows, idx] }]
            - set: [src, "${src}      - stage:\n          name: ${row.name}\n"]
            - set: [src, "${src}          agent: \"${row.label}\"\n          steps:\n"]
            - set: [src, "${src}            - step: { name: run_${row.name}, return: i32, body: [{ return: 0 }] }\n"]
            - set: [idx, { +: [idx, 1] }]
      - return: src

- def:
    name: run_generated
    args: [[rows, list<StageRow>]]
    return: i32
    body:
      - decl: [src, string, { call: [build_pipeline_source, rows] }]
      - decl: [job, Code, { call: [sys.code_from, src, { start: "run_generated", context: true }] }]
      - return: { call: [sys.exec, job] }
```

Why this detour is better than a composable type:

- **The checks run.**  The generated text goes through the same compiler as a written one:
  `resolved` without `resolve:` is an error, an unknown attribute is an error (F62-65), a
  typo in a parameter name is an error.  A generator that produces nonsense finds out
  — and before anything runs.
- **The key is right.**  What is hashed is the **generated** text (§8).  Two runs whose generator
  produces the same hit the same cache entry; if one line changes, only the
  affected step no longer hits.  An object composed at runtime would have no
  source text and therefore no stable key.
- **The matrix works.**  The generator may write `params:`; it is expanded when the
  generated text is compiled, not by the generator.  Whoever had to write out the lanes themselves
  would have lost the advantage.

- **F62-82** Generated pipeline source text is ordinary source text.  There is no second,
  laxer path for it — the same checks, the same key formation, the same errors.
- **F62-83** The generator should be **able to store** the generated text before executing it
  (`io.write`), because an error in generated text cannot be read without the text.  An
  error message with `line:column` (spec/49 `--check`) points to a line that is not
  written anywhere else.

## 14. Relation to the Host System

The language defines the form, the parameter space, the classification, the key and the checks.

| Concern | Where |
|---|---|
| local run, local store | this chapter, §12 |
| layer stacking, transfer between nodes | cosmos `doc/spec/92`, `96` |
| which node runs a lane | cosmos `doc/spec/90`, `91` |
| verify mode (cluster) | cosmos `doc/spec/96` §96.5 |
| pipeline/stage/step entities | cosmos `doc/spec/01` §1.4/§1.5/§1.5a |
| visualising a run (the matrix as a 3D scene) | cosmos `doc/spec/97` |

## 15. What Is Not Promised

- **Not bit-identical output.**  A compiler embedding a timestamp breaks byte equality while reuse
  stays useful.  The claim is "reusing the earlier result is as good as re-running" — weaker than
  byte equality, and achievable.
- **Not detection of a lying declaration.**  A `pure` shell step that downloads something will hit
  wrongly.  Verify mode finds it eventually; the compiler never will.
- **Not purity of the environment.**  An input a step reads without declaring — a file outside the
  workspace, a mounted socket — is invisible to the key.
- **Not total load control.**  See F62-10.

## 16. Open Questions

1. **Secrets in the key.**  (Partly decided for one case: registry credentials are not key
   material, F62-70, because there the value demonstrably has no effect on the result.)  `host:`
   values enter the key material as values (§8).  For a secret
   that means a hash, not plaintext — but it also means rotating a secret invalidates every
   dependent step, although the result does not depend on it.  A `secret: true` marking that keeps
   a value out of the key would fix that and opens a hole: a step whose behaviour *does* depend on
   the secret would then reuse wrongly.  Undecided.
2. **Lane workspace reuse.**  F62-13 gives every lane its own directory.  Across runs these
   directories can be kept (faster, incremental, but then a lane sees its predecessor's leftovers
   and `pure` becomes a lie) or discarded (correct, slower).  Discarding is the default unless a
   reason appears to do otherwise.
3. `vocab` and `tensor` are unclassified for F62-18.  Both carry process-global state
   (`vocab.load`, `tensor.begin_graph`), so neither is pure as a namespace, yet most of `tensor` is
   arithmetic.  Classify per function; until then both count as impure.
4. **Two executors.**  F62-23 makes the compiler generate the executor, and cosmos already owns an
   equivalent in aiml source (`CasStore`, `CasTrees`, `ActionCache`, `StepRunner`).  Either the
   generated code is the only executor and the host's classes shrink to storage and coordination,
   or the generated code stays thin and calls a host-provided interface.  Decide before implementing
   F62-23; building both and reconciling later is how the two drift apart.
5. **`outputs:` per step.**  *Decided 2026-11-27*, F62-168: paths and globs, relative to the
   workspace.  Capturing the whole workspace would always be correct but stores and transfers
   more than needed; a declared list can miss a file a later step needs — and says so, because a
   pattern that matches nothing fails the step.
6. **What the six statuses mean, and their precedence order.**  F62-42 fixes that there are six
   and what they are called; the meanings are open, and the aggregation order of F62-43 follows
   from them.  Decided so far: magenta = version incompatibility (F62-47), and lanes and
   pipelines carry the set of occurred colours next to the aggregate (F62-48).  The obvious reading treats the three primaries as facets that combine — failure,
   success, and "did not run" — which would make the mixtures "succeeded without running" (a cache
   hit), "failed without running" (a replayed failure) and "failure and success at once".  That
   reading is consistent and is *not* adopted here, because the naming should be settled once
   rather than inherited from a draft.  What is already decided: six values, mixture names, a
   total precedence order, no additive mixing for aggregates (F62-43), and outcome kept separate
   from lifecycle (F62-44).

## 17. `runtime:` — shared or separate execution context

A pipeline states **in what** its steps run:

```yaml
- pipeline:
    name: build_matrix
    runtime: shared | own          # default: shared
```

| Value | Meaning |
|---|---|
| `shared` | the host's context.  Libraries the host has loaded are visible; nothing is compiled again for this |
| `own` | a separate context with its **own symbol space**.  The pipeline brings along what it needs and sees nothing of the host |

- **F62-72** `runtime: own` is needed as soon as two pipelines need **different versions of the same
  library**.  With a shared symbol space there is exactly one entry per name —
  one version wins, the other disappears, **without an error message**.  That is the same
  class of error as a too coarse cache key (F62-64): it runs, and it runs wrongly.
- **F62-73** `runtime` is **key material** (F62-64, case 2).  The same step in a
  different context can run against different library versions and produce something different; a
  shared cache entry would be a false hit.
- **F62-74** Which libraries a pipeline needs is stated on the pipeline, not on the host:

  ```yaml
    libraries:
      - { name: acme_build, version: "2.1" }
  ```

  With `runtime: own` exactly these are loaded.  With `runtime: shared` it is checked whether the host
  has them in **this** version — if one is missing or the version differs, that is an error before the
  first step, not a silent false hit in the middle of the run.
- **F62-75** The version belongs in the key, not only in the requirement.  Otherwise a
  step built against `2.0` hits for a run with `2.1`.

**What this demands of the runtime** (aiml `spec/29_2` §16/§17): a symbol space **per context**.
Today's way via `DynamicLibrary::AddSymbol` is flat, process-wide and, according to the LLVM header,
"permanent" — that makes neither a second version nor unloading possible.  What is needed is either
a separate symbol resolver per engine (staying with MCJIT) or ORC's **JITDylibs**, which are made
for exactly this.  The version question decides this point, not speed.

## 18. Target picture — `image:` and `push:`

Not a requirement of this chapter and not on its critical path (§2).  Written down because the
shape should be fixed **before** someone adds it, and because F62-64 demands an answer about key
material that is easy to get wrong once and then wrong forever.

```yaml
- pipeline:
    name:  build_matrix
    image: "docker.io/library/ubuntu:24.04"     # optional base for every stage
    push:                                       # optional, may be a list
      - { from: package, to: "ghcr.io/acme/service", tag: "${mode}-${arch}" }
    params: [...]
    steps:
      - stage:
          name: compile
          image: "ghcr.io/acme/builder:17"      # overrides the pipeline's
          steps: [...]
```

Both are **optional**, and absent they change nothing: a pipeline without `image:` runs against the
node's own filesystem exactly as it does today.

- **F62-66** `image:` sits on the pipeline as the default and may be overridden by a **stage**,
  never by a step — the same reason as `runs_on` (F62-60): the filesystem a workspace lives in
  belongs to the stage.
- **F62-67** `image:` **is** key material, as its own component (F62-64 case 2).  A base filesystem
  changes what a step produces without appearing anywhere in the step's source, so a step built
  against two different bases must not share one entry.
- **F62-68** What enters the key is the **resolved digest**, never the tag.  `ubuntu:24.04` is a
  moving name in exactly the sense of §1.1, so an image is the `resolved` class (§7) applied to a
  base: a cheap registry request turns the tag into `sha256:…`, the probe runs every time, and the
  key carries the digest.  This is the same split as `git ls-remote` and needs no new concept —
  only a different probe.
- **F62-69** `push:` is a **side effect and never cacheable**, but what it pushes is.  The two
  halves separate cleanly: serializing a stage's output into a layer is deterministic and therefore
  reusable (cosmos `doc/spec/96` F96-19/A96-12), while the act of pushing is `opaque` by nature and
  must run every time it is asked for.  A step that pushes can never be `pure`.
- **F62-70** Registry credentials are `host:` names, never values in the source (F62-17), and never
  key material: a rotated token must not invalidate a build whose result does not depend on it.
  This is the one place where §16 item 1 is already decided, because here the value demonstrably
  has no effect on what is produced.
- **F62-71** `push.from` names a stage, not a step: an image is a filesystem, and the filesystem is
  what a stage owns (F62-60).  `tag` may interpolate parameters, which is how a matrix publishes
  one tag per lane.

- **F62-122** `to:` names a target, and the target may carry a **scheme**.  Without one it is a
  registry reference as above.  `oci:<path>` is an **OCI Image Layout on disk** — the same
  directory shape a registry serves, only local: `oci-layout`, `index.json`, `blobs/sha256/…`.
  This is not a second format and not a special path, but the same contract without a network; cosmos
  already writes it today (`cosmos/doc/spec/110` §110.25).  It is the form a
  build pipeline needs before there is a registry — and the one with which a run lays down its result
  for the **next** run as a base (`image:` reads the same address).
- **F62-123** Serialisation happens where the tree is (cosmos F96-27), and that also holds locally:
  the node writes the layout, the controller only remembers the address.  If the target is on
  a different machine from the tree, the blobs travel via the content-addressed path from
  cosmos §96.2 — not via the controller.

What this needs beyond the form: a root filesystem to start from, a deterministic layer
serialization, whiteouts for deletions and full file metadata.  All four are collected in cosmos
`doc/spec/96` §96.12 with their findings; none of them touches the stage/step form.

### 18.1 `image:` without a runtime — and the local run

A pipeline with `image:` requires a **root filesystem**.  The local JIT run (§12) has
none: it runs on the machine on which someone called `run_<name>`.  That is not a
defect — it is the fast path, and it should stay.  But it is not the same either.

- **F62-124** `image:` without a provider is an **error**, not a hint and not a silent
  fallback to the developer's filesystem.  The reason is F62-65 in its sharpest
  form: a run that builds against the laptop's `/usr` and is still called "against ubuntu:24.04"
  is worse than a run that does not start at all.  The message names **three** things — the
  requested image, that there is no runtime here, and the way to get there (a run through a
  host that can do containers).
- **F62-124a (addendum 2026-10-25): rejection happens at RUN time, not at compile time.**  The
  reasoning of F62-124 is about a **run** that claims to have run elsewhere — not about a
  file that names an image.  Until now the rewrite already rejected compilation; that meant
  a pipeline intended for a cluster could **never** pass `--check`, and a
  controller had to read it itself (§20.1 — exactly the duplication that is supposed to go away).  So: `image:`,
  `push:`, `agent:`, `steplayer:`, `base:` compile and are available as data in `pipeline`/`stage`
  (§20.4); **`pipe.run` locally** returns **-4** and refuses with the same message F62-124 demands — three things,
  including the way.  The promise is kept literally, it just applies at the place where it holds.
- **F62-125** Without `image:` a pipeline runs **against the machine**, and that is the default.
  The local run needs no container concept for this (§12) and should not get one.
- **F62-132** The check happens on **both levels** — at the head of the pipeline and at every stage.
  An `image:` at the head is the more common case (it applies to all stages), and exactly there
  silently dropping it would be most expensive: the author reads "ubuntu-base", the run takes the host,
  and nobody sees the difference in the result.  Negative probes:
  `tests/pipeline_declared_neg/image_rejected.aiml` (head) and `push_rejected.aiml`.

### 18.1a `resources:` — what machine the stage needs

```yaml
- pipeline:
    name: aiml_bauen
    image: ubuntu-base
    resources: { cpu: 4, mem: "8G", pids: 2048 }    # applies to all stages
    steps:
      - stage:
          name: bauen
          resources: { cpu: 8, mem: "12G" }          # overrides it
```

- **F62-133** `resources:` names **limits**, not wishes: `cpu` in whole cores, `mem` with a
  unit (`512M`, `8G`), `pids` as a number.  The host sets them as cgroup limits of the
  container (cosmos `doc/spec/110` §110.34).  Without them, whatever the host prescribes applies — a
  pipeline that says nothing gets the default and not "unlimited".
- **F62-134** `resources:` is **not key material** (F62-64, case 3), and that is a
  statement about *results*, not about convenience: more cores make the same build
  faster, not different.  A too tight **memory limit** can, however, kill a step
  — but then it **fails**, and a failure is not recorded (F62-92).  A
  limit can therefore turn a success into a failure, never into a *different success*;
  that is why the key stays clean and a cache hit stays correct.
- **F62-135** The limit belongs in the **pipeline**, not in the host, as soon as a stage needs something
  special.  `COSMOS_RT_LIMIT_MB` applied equally to every container — that means either
  every stage gets what the most expensive one needs (and a node carries only one), or the most expensive one
  dies.  Both are wrong, and neither is written in the pipeline, where someone could
  look it up.

### 18.1b `shares:` — a network drive in the container

Cores and memory are one thing; but some stages need **space that outlives the run**:
a network drive with sources, a storage directory for artefacts, a package cache that is not
pulled from the network anew on every run.

```yaml
- pipeline:
    name: aiml_bauen
    image: ubuntu-base
    shares:
      - { name: quellen, to: /mnt/quellen }              # read (default)
      - { name: artefakte, to: /mnt/out, mode: rw }      # write
    steps:
      - stage:
          name: bauen
          shares:
            - { name: ccache, to: /root/.ccache, mode: rw }   # additionally, only here
          run: "ls /mnt/quellen && make -j8"
```

- **F62-136** `shares:` names a **NAME**, never a path.  Where the storage lives — an
  NFS mount, an SMB share, a directory on disk — is known to the **host**, just
  as with `image:` (F62-66/67; cosmos `doc/spec/110` §110.35).  The reason is not taste:
  a pipeline is user input.  If it could write `/srv/...` or `/`, every
  submitted pipeline could pull any directory of the host into its container — and with `mode: rw`
  also write to it.  A name that the operator mapped beforehand cannot do that.
- **F62-137** The default is **`ro`**.  Writing has to be stated.  A share that a run
  accidentally writes to damages everyone else's runs; that is the most expensive error in
  this corner and therefore the one that demands intent.
- **F62-138** `shares:` of the pipeline and of the stage **add up**; the same `to:` in both
  places means the stage wins.  The same rule as for `resources:` — a stage may need something
  special without the pipeline being rewritten for everyone.
- **F62-139** `shares:` **is** key material (F62-64, case 1), but only with `name` and `to`
  — not with the content.  What a step sees at `/mnt/quellen` can change between two
  runs without anyone noticing; the key cannot represent that, and
  a record that pretends otherwise would be a lie about reproducibility.  The honest consequence
  is already in §7: the default is `cache: opaque`.  Whoever still declares a stage with `shares:` as
  `pure` claims that the content of the share belongs to the key — and must take care of that
  themselves (for instance by making the share immutable).

### 18.4 `base:` — building on the layers of another run

`image:` says **in what** a run works.  `base:` says **on what** it builds:

```yaml
- pipeline:
    name: 9rooms_bauen
    base: "job:aiml_bauen"               # the most recent SUCCESSFUL run of this job
    # base: "run:aiml-vom-Base-Image-5"  # or: exactly this run
    steps:
      - stage: { name: bauen, run: "exodus build -c exodus-game.json" }
```

- **F62-140** `base:` takes the **layer stack** of an earlier run as the foundation.  It is
  read from, **writes always go into the own layer** — the other run remains untouched,
  and two runs can use the same foundation at the same time.  The form today is
  `run:<run>`; a name that the operator maps can follow, but changes nothing about the
  meaning.
- **F62-141** `base:` applies to the **whole run**, not to a stage.  A stack is the
  ground everyone stands on; a stage that stood on different ground from its predecessor
  would no longer be one run but two.
- **F62-142** `base:` is **not a replacement for `image:`** but the other direction.  An image
  is the way **out**: it carries everything within itself, can be copied away and deployed
  elsewhere.  A stack is the way **next door**: it costs nothing, but presupposes that both
  runs stand on the same host and the same base image (cosmos `doc/spec/110` F110-114).
  Whoever mistakes one for the other builds an image that only works at home.
- **F62-144** `base:` has two forms.  `run:<run>` names **exactly this** run;
  `job:<id>` takes the **most recent successful** run of this job.  "Last run"
  without "successful" deliberately does **not** exist: a failed run has left behind half a
  layer, and building on it would mean building on a state that nobody has ever
  seen.  Whoever wants exactly that (to investigate a broken stage) names the run explicitly
  — then the intent is written in the pipeline and is not the silent default.
- **F62-145** `job:…` is resolved **when the run is created**, not on every dispatch.
  After that `run:<run>` is in the record, and the log names both
  (`'job:aiml-vom-Base-Image' → run:aiml-vom-Base-Image-5`).  A foundation that changes in the middle of the
  run would be a run on two different grounds; and a run whose basis can no longer
  be named afterwards is not traceable.
- **F62-143** What is to be passed on belongs in the **root**, not in the
  workspace.  `/ws` is the run's workspace and does not travel along; an editable
  install there (`pip install -e`) leaves a dangling reference in the stack
  (`doc/spec/110` F110-115).  Install it properly, then it is in the layer.

### 18.5 `agent:` and `steplayer:` — who takes the step, and does a layer come into being?

Two questions that previously were packed into one word (`runs_on:`) or not asked at all:

```yaml
- pipeline:
    name: pruefen
    steps:
      - stage: { name: schnell, agent: x86_64, steplayer: none, run: "make -j8 test" }
      - stage: { name: sauber, steplayer: own, image: ubuntu-base, run: "./von_vorn.sh" }
```

- **F62-146** `agent:` says **WHO** takes the step — a **label** (any agent with this
  tag) or an **agent id** (exactly this one).  The name is deliberately not `node:`: a
  container agent is not a node, it lives on one.  What executes is an
  agent in both cases.  (Called `runs_on:` until 2026-10-25, before that `label:`; both old names are rejected
  and name the new one.)
- **F62-147** `steplayer:` says **WHETHER** every stage writes into its **own layer**: `own`
  (own root from an image, a layer per stage, own workspace — cosmos
  `doc/spec/110` F110-93) or `none` (no layer building; the step works on the
  agent's filesystem, confined with bwrap).  Without it, `image:` decides: if there is one,
  `own` is meant, otherwise `none`.  The setting is never necessary and always allowed — and
  `steplayer: none` **together with** `image:` is a contradiction that the submission rejects,
  instead of letting one of the two win.
- **F62-148** `steplayer:` is **key material** (F62-64, case 1).  The same command
  on an agent's filesystem and in a fresh root sees different things — a
  shared cache entry would be a false hit.  It therefore belongs in the step key
  like `image:` itself.
- **F62-149** The dispatcher **does not choose**.  It does not fall back to a node because no
  container is free, and it does not start a container because the nodes are busy.  Whoever
  wants fallback writes two stages and says which applies when — then the intent is in
  the pipeline and not in the server's mood (cosmos `doc/spec/110` F110-124).

### 18.6 `secrets:` — a credential for a stage

A stage that checks out a private repository, publishes a package or signs an artefact needs a
credential.  The pipeline names it; the host knows what the name stands for.

```yaml
- pipeline:
    name: publish
    secrets:
      - { name: gh_token, env: GH_TOKEN }
    steps:
      - stage:
          name: upload
          secrets:
            - { name: deploy, env: DEPLOY }
          run: 'curl -u "$DEPLOY_USER:$DEPLOY_PASSWORD" -T out.tgz https://repo.example/'
```

- **F62-165** A secret is a **NAME** bound to an **environment name**, never a value.  The same
  reason as F62-136: a pipeline is user input and lives in a repository, and what is in a
  versioned file is published.  An entry is `{ name, env }` and nothing else — a `value:` is an
  error, not an ignored key (F62-65).  `env` is `[A-Z_][A-Z0-9_]*`, because the host may derive
  further names from it (`DEPLOY_USER`, `DEPLOY_PASSWORD` for a username/password secret); names
  that belong to the executor (`PATH`, `HOME`, `TMPDIR`, `COSMOS_*`, `LD_*`, …) are refused.
  What a kind of secret is and how it reaches the step is the host's (cosmos `doc/spec/115`).
- **F62-166** `secrets:` of the pipeline and of the stage **add up**; the same `env:` in both
  places means the stage wins.  The same rule as F62-138.  The reader `stg.secrets` gives the
  resolved list as `name:ENV,…`.
- **F62-167** A stage with secrets is **opaque** (F62-64, case 3: deliberately not key material).
  The value cannot be key material — the key would carry it into every store it travels to — and
  without it a layer built with one credential would be handed to a run that has another.  An
  inherited `cache:` falls to `opaque`, for the stage and its steps; a `cache: pure` or
  `resolved` said on such a stage or one of its steps is an error, because it claims a reuse that
  cannot be true.  The composed form reads `opaque` as well.

### 18.2 `requires:` — what the environment must bring along

The same pipeline runs locally and in a container.  Locally the machine decides whether `git`, `gcc`
or `llvm-config-18` are present; in a container the image decides.  In both cases:
**if something is missing, it should be noticed in the first second and not in the fifth minute**
— in the middle of compiling, with a message about a missing header file that leads nobody to the
missing package.

```yaml
- pipeline:
    name: aiml_bauen
    image: "oci:deploy/oci/ubuntu-base"
    requires: [git, "gcc>=13", llvm-config-18, "lib:yaml-cpp"]
```

- **F62-126** `requires:` is a list of **names**: an executable program (`git`), with
  an optional minimum version (`gcc>=13`), or a library via `lib:<pkg-config name>`.
  It is checked **before the first step** of the stage, **once**, and the message names **everything**
  that is missing at once — not the first thing one stumbles over.  A check that stops after the
  first error produces three runs instead of one.
- **F62-127** `requires:` is **not key material** (F62-64, case 3).  That a tool
  *is present* changes no result — that it is a *specific version* very much does, and that is
  exactly what `host:` is for (§10).  The separation is the rule from F62-49, only one level deeper:
  checking a precondition ≠ putting a property into the key.
- **F62-128** `requires:` does **not** replace `image:` and does not explain it away either.  It is the
  promise a pipeline makes about its environment; `image:` is the environment.  A pipeline
  may name both — then the container checks against its own promise, and an image that
  cannot carry its own pipeline is noticed when the image is built, not when the
  product is built.

### 18.3 Causality: an image that a stage first produces

`push:` writes an image, `image:` reads one — and both may appear in the **same** pipeline.
Exactly then an order arises that nobody wrote down:

```yaml
    steps:
      - stage: { name: basis,  push: [{ from: basis, to: "oci:deploy/oci/builder" }], ... }
      - stage: { name: bauen,  image: "oci:deploy/oci/builder", ... }
```

- **F62-129** If a stage names as `image:` a target that **another stage of the same
  pipeline** writes, that is an **edge in the graph** — the same effect as a `needs:`,
  only derived instead of written down.  Without this derivation the second stage would run against the
  image of the **previous run**: green, wrong, and different again next time.  The host
  derives the edge by comparing the addresses and reports a cycle as an error.
- **F62-130** What goes into the key is the **digest of the written layout**, not its path
  (F62-68 for tags, here for local targets).  A path is a name and moves; the digest
  is the content.  Only with this is a run that builds an image and continues building in it
  reproducible — and only with this may the second stage come from the cache.
- **F62-131** An `image:` that points to a target that **nobody** in this pipeline writes
  and that does not exist either is an error **before** the start — not when the stage is dispatched.
  The run that works for ten minutes and then fails at the last stage because its image
  never existed is the most expensive way to find out.

## 19. Implementation status (2026-11-27)

This spec describes more than runs today.  So that it makes no promises the code does not
keep, this section states what **measurably** works and what is open.  Every line names the test
that holds it — a line without a test counts as not implemented, no matter how convincing the code
looks.

### Implemented and tested

| Topic | Where | Test |
|---|---|---|
| Matrix, lanes, `exclude` as partial binding (§4) | `pipeline_runtime.cpp` | `tests/test_pipeline_matrix.aiml` |
| `pipeline`/`stage`/`step` as values (§13.5) | `builtin_registry.cpp` | `tests/test_pipeline_types.aiml` |
| Observer per (step, lane) (§13.2) | `AimlStepObserver` | `tests/test_pipeline_observer.aiml` |
| `args` subset = coarser key (§5.2) | `step_key` | `tests/test_pipeline_args_subset.aiml` |
| Reuse classes + probe (§7, §9) | `step_key`, `set_resolve` | `tests/test_pipeline_resolve.aiml` |
| `on_fail`, `timeout`, `retry`, status (§5.6, §5.7) | `run_lane` | `tests/test_pipeline_failure.aiml` |
| Pause/resume (§5.9) | `request_pause`, `release` | `tests/test_pipeline_pause.aiml` |
| Parallel lanes (§6) | `run_lane` + threads | `tests/test_pipeline_parallel.aiml` |
| Concurrent process start (F62-14) | — (measurement) | `tests/test_concurrent_spawn_hazard.aiml` |
| Lane view in the body, workspace (§4.3, §13.5e) | `step.arg`/`step.workspace` | `tests/test_pipeline_lane_access.aiml` |
| Purity check (§10.2) | `purity.h` | `tests/pipeline_purity/run.sh` |
| Declared form (§5) | `pipeline_expand.cpp` | `tests/test_pipeline_declared.aiml`, `tests/pipeline_declared_neg/run.sh` |
| Body over the wire (§5.8, part) | `sys.bundle`/`sys.exec` | `tests/bundle_socket/run.sh` |
| `requires:` checked locally (§18.2) | `pipeline_expand` + `sys.require_tools` | `tests/test_pipeline_requires.aiml` |
| `sys.require_tools` (spec/23 §5.6) | `process_runtime.cpp` | `tests/test_sys_require_tools.aiml` |
| `image:`/`push:` rejected locally, with the way (§18.1 F62-124/132) | **at RUN time** (F62-124a), not at compile time | `tests/test_pipeline_local_refuses.aiml` |
| `image:` as a dispatch criterion (§18, F62-66/67) | cosmos `step_scheduler`, `rt_launcher` | cosmos `test/test_step_dispatch.aiml` |
| `push: oci:<path>` + derived order (F62-122/129) | cosmos `push_worker`, `cluster_bootstrap` | cosmos `test/test_step_dispatch.aiml` + `doc/spec/110` §110.30 (measurement) |
| Persistent step store, across processes (§12.2, F62-24/24a) | `store_load`/`store_save` | `tests/pipeline_store/run.sh` |
| Typed data flow: models through the result channel (F62-33/34, F62-118a) | `pipeline_expand` + `sys.serialize` | `tests/test_pipeline_typed_flow.aiml`, `tests/test_bundle_model.aiml` |
| Fan-in: `per: once` over a `per: lane` step, `list<T>` (F62-8, F62-121a) | `need_results`, lane-ordered `input_keys` | `tests/test_pipeline_fanin_key.aiml` |
| `timeout` enforced: the step's process tree is killed (F62-40/40a) | `StepWatch`, `aiml_step_note_child` | `tests/test_pipeline_timeout_kill.aiml` |
| `outputs:`, the output tree in `blobs/`, restored on a hit (F62-168) | `capture_outputs`/`restore_outputs` | `tests/test_pipeline_outputs.aiml` |
| Stage `per`/`needs`, inheritance pipeline → stage → step (F62-58a, F62-111) | `pipeline_expand` | `tests/test_pipeline_stage_inherit.aiml`, `tests/pipeline_declared_neg/run.sh` |
| `ExeOptions` as a built-in model, runtime `shell`/`capture`/`detach` (F62-27/27a/29) | `injectBuiltinModels`, `aiml_process_spawn_opts` | `tests/test_exe_options.aiml` |
| `exec_defaults`, `host_env`, host values in the key (F62-61, §5.2, §8) | `pipeline_expand`, `step_key` | `tests/test_pipeline_exec_defaults.aiml` |
| The examples of §11 and §13.6 compile | — | `tests/pipeline_spec_examples/run.sh` |


### Open, with reasons

What remains open is the **host's** part, not the language's: this chapter fixes the form, the key,
the classes, the store format and the local executor, and all of it runs.

- **`agent:` and real transport to a node (§5.8, §18.5).**  The bundle demonstrably travels
  over a connection (`tests/bundle_socket`), and `agent:` is carried as a stage attribute that a
  reader sees (§20), but a local run selects no node — that is the host's scheduler (cosmos).
- **Moving an output tree between nodes (F62-48).**  Locally the tree lies in the store and is
  restored from there (F62-168); shipping blobs to another node is the host's transport.
- ~~Concurrent compilation (F62-99)~~ — **measured and decided** (F62-99): threads
  bring nothing for isolated compilation (8 threads ~1030 ms versus single-threaded ~960 ms for
  the same 24 compilations); the lock stays.
- **Container URL and push (§17), 3D display.**  Target picture for the host; locally `image:` and
  `push:` are refused with the way (F62-124a).
- **The meaning of the six colours (§16 item 6).**  Deliberately open; the precedence order exists
  as a provisional table in one place (F62-93).

## 20. READING the compiled pipeline — a controller is a reader, not a parser

**User requirement (2026-10-25):** *"The idea was that the pipeline code is pushed through the compiler.
So cosmos itself does not resolve any aiml nodes."*

### 20.1 Why

Today **two** programs read the same language: the compiler (`pipeline_expand.cpp`, §5) and
the controller (cosmos `sources/server/pipeline_submit.aiml`, 180 `yaml.*` calls).  Both
know `params`, `exclude`, `stages`, `steps`, both form lanes, both check forms — and they
know **different parts**: the compiler purity and the key, the controller
`agent:`, `steplayer:`, `image:`, `base:`, `shares:`, the limits.  Two readers of the same language
drift apart, and they do so quietly: a key that one knows and the other does not is
either a silent no-op or a rejection without reason.

The direction is therefore not "the controller learns the remaining keys", but **the
controller does not read at all any more**: it hands the source text to a context (spec/29_2 §14a/§14b),
has it compiled and **reads the resulting pipeline as a value**.

```yaml
# The whole way, without a single line of YAML in the controller:
- decl: [kontext, ctx_handle, { call: [ctx.new] }]
- decl: [rcx, i32, { call: [kontext.exec, { call: [sys.code_from, eingereicht] }] }]
- if: { condition: { lt: [rcx, 0] }, then: [ ... kontext.errors an den Einreicher ... ] }
- decl: [namen, list<string>, { call: [kontext.names, "fn() -> pipeline"] }]
- decl: [pipe, pipeline, { call: [kontext.call, { at: [namen, 0] }, "fn() -> pipeline"] }]
- decl: [lanes, i32, { call: [pipe.lane_count] }]
- decl: [stufen, i32, { call: [pipe.stage_count] }]
```

### 20.2 Who checks what

| | checks | Example |
|---|---|---|
| **Compiler** | the **form**: keys, types, axes, references, purity, contradictions | `steplayer: own` without `image:`; `needs: [gibtesnicht]`; unknown key; `exclude` on an axis that does not exist |
| **Controller** | the **situation**: what exists here and who is allowed | does this cluster know the image `aiml-build`?  is the label `x86_64` on an agent?  may this user have this many lanes? |

- **F62-150 Form errors belong to the compiler, situation errors to the controller.**  The dividing line is
  whether the answer is in the source text: "`steplayer: own` without `image:`" can be decided in the source
  text and is therefore a **compile error** — the same everywhere, also in a local run, and
  written only **once**.  "This image does not exist here" depends on the machine
  and is therefore the controller's business; a compiler that claims it lies on every second
  cluster.  The old names (`runs_on:`, `label:`, `runs_in:`) are therefore form errors as well:
  **the compiler** names the new name, not every host on its own.

### 20.3 What the compiler has already done before reading

- **Lanes formed**, `exclude` subtracted (§4) — the reader gets the number and, per lane, its
  **binding text** in the canonical form `axis=value,axis=value` (sorted, §8).  Exactly the
  form that the observer (§13.1) and the workspace path (§4.3) carry too.
- **Precedence resolved** (§18, F62-133/F62-138): `image:`, `timeout:`, `requires:`, `agent:`,
  `steplayer:` and the limits are on the stage with the value that **applies** — stage beats
  pipeline; `shares:` add up.
- **Defaults derived** (F110-125): `steplayer` is `own` if an `image:` applies, otherwise `none`.

- **F62-151 Precedence is resolved ONCE, in the compiler.**  Otherwise every host recomputes the
  rules, and two hosts at some point disagree about what `timeout:` on the pipeline means for
  a stage with its own `timeout:`.  A reader that has to recompute is a second
  parser with a different name.
- **F62-152 The binding text is ONE format.**  Lane text, observer `bindings`,
  workspace name and the controller's record (cosmos F96-34) carry the same
  string.  Two formats for the same concept mean: somewhere display text is being parsed.

### 20.4 The readers

All **read-only**; a value that has been read does not change the pipeline.

| Pipeline | returns | Meaning |
|---|---|---|
| `pipe.name` | string | name from the declaration |
| `pipe.lane_count` | i32 | lanes after `exclude` (§4.3) |
| `pipe.lane_bindings(i)` | string | `arch=x86_64,mode=debug` (F62-152) |
| `pipe.stage_count` | i32 | stages per lane |
| `pipe.stage_at(i)` | stage | the i-th stage |
| `pipe.axis_count` / `pipe.axis_name(i)` | i32 / string | the axes in declaration order |
| `pipe.axis_value_count(i)` / `pipe.axis_value(i, j)` | i32 / string | their domain (§4.1) — count and value, **no list**: returning a `list<string>` from the runtime costs a filling interface per reader, and two number readers say the same thing |
| `pipe.push_count` / `pipe.push_from(i)` / `pipe.push_to(i)` / `pipe.push_tag(i)` | i32 / string | `push:` (§18, F62-69/71) |

| Stage | returns | Meaning |
|---|---|---|
| `stg.name` | string | |
| `stg.agent` | string | **WHO** — label or agent id (F110-127) |
| `stg.steplayer` | string | **WHETHER** — `own` \| `none` (§18.5, F110-124) |
| `stg.image` | string | **IN WHAT** — image or `oci:<path>` (F62-66) |
| `stg.base` | string | **ON WHAT** — `run:<run>` \| `job:<id>` |
| `stg.requires` | string | `git,gcc>=13,lib:yaml-cpp` (§18.2, F62-126) |
| `stg.shares` | string | `name:/target:mode,…` (§18.1b, F62-136) |
| `stg.secrets` | string | `name:ENV,…` (§18.6, F62-165/166) — names, never values |
| `stg.cpu_cores` / `stg.mem_mb` / `stg.pids_max` | i32 | limits, 0 = host default (§18.1a) |
| `stg.timeout_ms` | i32 | 0 = executor default (F62-40) |
| `stg.command` | string | `run:` — the command of slice 1 (cosmos F103-1) |
| `stg.cache` | string | reuse class after F62-159/161/167: `pure` \| `resolved` \| `opaque` |
| `stg.resolve` | string | the probe line of a `run:` stage (F62-160), otherwise empty |
| `stg.step_count` / `stg.step_at(i)` | i32 / step | the steps of the stage (§5) |

| Step | returns | Meaning |
|---|---|---|
| `stp.name` | string | |
| `stp.needs_count` / `stp.need_at(i)` | i32 / string | `needs:` (§5.1) |
| `stp.cache` | string | reuse class (§7), inherited after F62-159 |
| `stp.per` | string | `lane` \| `once` (§5.5) |
| `stp.command` | string | `run:` on the step, otherwise empty |

- **F62-153 A reader returns values, not pointers into the compiler.**  `stage` and `step` are the
  values from §13.5 — the same ones that can be composed.  There is no way from the value read
  to the AST, the LLVM type or the symbol table (spec/29_2 F29_2-…m): otherwise the
  controller depends on the compiler's internals.
- **F62-154 If something is missing, it is empty or 0 — not "unknown".**  A reader does not throw because
  a key was not written; for the limits `0` explicitly means "the host's
  default" and not "unlimited" (§18.1a).  An index out of range, on the other hand, is a
  programming error of the reader and **throws** (spec/22).
- **F62-155 The stages are lane-INdependent, the bindings lane-dependent.**  Hence `stage_at(i)`
  without a lane and `lane_bindings(i)` without a stage: the reader builds the cross product, because only it knows
  whether it needs one row per (lane, stage) (cosmos) or per stage (an overview).  A
  reader function that mixes both would have to take `(lane, stage)` and describe the same
  stage anew for every query.

### 20.4a What else was noticed during the rework (2026-10-25)

Three places where spec and implementation had diverged — found because the controller
has since been using the **same** reader as the compiler.  Each is recorded here so that they
do not drift apart a second time:

- **F62-156 `steps:` on both levels, as F62-57 says.**  The rewrite read `stages:`,
  the spec wrote `steps:` — and cosmos' 15 pipelines were spec-compliant all along, the
  compiler was the outlier.  Now `steps:` applies; `stages:` is rejected and names the
  correct key.  The same for `params:`: the rewrite took the mapping
  `arch: [...]`, §4.1 requires the **typed list** `[name, type, [values]]`, and cosmos'
  tests used the list.  The type now takes effect: in the body an `i32` axis is an `i32`
  (F62-1), converted in **one** place in the generated preamble.
- **F62-157 The name is text, the function needs an identifier.**  `name: 9rooms_vom_stapel`
  starts with a digit and therefore could not be compiled at all — although the name is perfectly
  fine.  If the name is an identifier, the function is called exactly that (F62-108 stays; one
  calls it locally); otherwise a derived function name is created, and the **value** keeps carrying
  the real name (`pipe.name`).  A host finds the function via `cx.names`
  (spec/29_2 F29_2-…i) and does not need to guess the name.
- **F62-158 A compiler REPORTS, it does not fall over.**  `cache: haeufig` produced the generated
  call `set_cache(CacheClass.haeufig)`; enum resolution returned `nullptr`, and the
  builtin method kept calculating with it — a crash in the compiler, on exactly the input it was
  complaining about.  Two lessons, both implemented: `cache:` is checked in the rewrite
  (§7), and an argument that could not be compiled stops the builtin method.  With
  a **submitted** pipeline this is not a cosmetic flaw: a crash of the controller
  is a submission that terminates the service.

### 20.5 What this does NOT mean

- **No run.**  Reading executes nothing; `pipe.run` remains the local run (§12).  A
  controller reads and dispatches **itself** — distributed execution is its job, not that
  of the language (§14, F62-49).
- **No clearance.**  The context is not a security boundary (spec/29_2 F29_2-…e).  Whoever compiles foreign
  source text compiles it **in their own process**; the boundary for foreign code remains
  a separate process (spec/65 F65-15).  For cosmos this means: the submission is checked, the
  **body** runs at the agent, not in the controller.
- **No protection against hostile source text.**  Values that go as a literal into generated source
  text are **escaped** (`"` → `\"`, line break → `\n`) — without that the literal would end
  in the middle of the value and the rest would be aiml, i.e. injected code.  Escaped and not
  rejected, because a `run:` legitimately carries quotation marks.  That makes the rewrite
  **robust**, not secure: for hostile submissions the boundary remains a separate process
  (spec/65 F65-15), and a controller does not run submitted source text
  (`cx.load`, spec/29_2 F29_2-…n).
- **No second format.**  There is no "controller dialect" of the pipeline language.  What the
  compiler does not know is an error — even if a host could deal with it.
