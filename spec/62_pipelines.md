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
  anywhere.
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
  lanes of that step.  A `per: lane` step that `needs` a `per: once` step receives its single
  result.  Both directions are ordinary dependencies; no separate join syntax exists.
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
  between them.  On expiry the step's children are killed (`process.kill()` kills a detached job's
  whole group, `spec/23` §5.2) and the step counts as failed.  This is not the same as
  `ExeOptions.timeout`, which bounds one child: a step that starts forty compilers needs a bound
  on the step.
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
                  - call: [fail_pipeline, "Quellbaum unbrauchbar — jede Lane waere sinnlos"]
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

#### Das Messergebnis (2026-10-11)

`tests/test_concurrent_spawn_hazard.aiml` ist diese Messung: **acht Threads starten je zwoelf
Prozesse**, also 96 Starts mit maximaler Ueberlappung, jedes Kind mit **eigenen** Pipes
(`capture: true`).  Geprueft wird nicht die Geschwindigkeit, sondern dass jeder Start gelingt,
jeder Exit-Code richtig ankommt und **keine Ausgabe in einen fremden Thread wandert** — die
Deskriptor-Haelfte der Gefahr, die cosmos beim geerbten Listener schon getroffen hat.

Drei Laeufe: `96/96` gelungen, `0` fehlgeschlagen, `0` vertauscht, ~12 ms.

- **F62-101** `parallel > 1` ist damit **freigegeben**, mit zwei Auflagen, die aus der Messung
  folgen und nicht aus einem Argument:
  - Ein Schritt, dessen Ausgabe gelesen wird, laeuft mit `capture: true`.  Ohne das erbt das Kind
    stdout des Elternprozesses, und mehrere Lanes schreiben in denselben Strom — beim ersten
    Messversuch sah das wie `96` vertauschte Ausgaben aus und war in Wahrheit die fehlende Pipe.
    Der Unterschied ist wichtig: die Gefahr ist nicht, dass Pipes versagen, sondern dass gar
    keine da sind.
  - Die Messung gilt fuer `sys.exe` aus Threads.  Sie sagt **nichts** darueber, ob mehrere
    isolierte Uebersetzungen (`sys.exec`, §13) gleichzeitig laufen duerfen — das ist LLVM-Zustand,
    nicht Prozess-Zustand, und braucht eine eigene Messung (F62-99).

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
<step_source_hash> <lane_bindings> <input_keys> <host_values> <resolve_output>
```

- `step_source_hash` — hash of the step's body **as written**, before parameter substitution.
- `lane_bindings` — the bindings of the parameters the step **declared** in `args` (§5.2), sorted
  as `name=value`, with the count included.  Not the whole lane: a step that does not declare an
  axis is reused across it.
- `input_keys` — the keys of the steps named in `needs`, sorted.  A changed input changes the key
  transitively, which is what makes a chain of steps behave like a stack of layers.
- `host_values` — the values of the names in `host:`, sorted, count included.
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
- **F62-89** Die Sonde läuft im Kontext des **Aufrufers**, der Rumpf im **isolierten** Kontext.
  Gemessen beim Schreiben von `tests/test_pipeline_resolve.aiml`: eine globale Variable, die die
  Sonde hochzählt, wirkt; dieselbe Zuweisung im Rumpf meldet `Undefined variable`, weil ein Bundle
  Code trägt und keinen Prozesszustand (`spec/29_2` §13).  Das ist beabsichtigt und nützlich — die
  Sonde darf den Zustand des Wirts lesen (Konfiguration, Netzwerk, Arbeitskopie), der Rumpf nicht.
  Was ein Rumpf vom Wirt **doch** sieht, steht in `spec/29_2` §22: eingebaute Funktionen ja,
  Wirts-Funktionen als mitgebuendelte **Kopie**, globale Variablen nur als frisch initialisierte
  Deklaration — und Dateien, Datenbank und Prozessrechte **geteilt**.  Die Trennung ist
  lexikalisch und keine Sandbox (F29_2-22b); eine Pipeline, deren Quelltext von aussen kommt, ist
  darum dieselbe Fehlerklasse wie eine Remote-Shell.
  Wer im Rumpf zählen oder messen will, nimmt die Laufzeit-Zähler (`ran_count`/`reused_count`,
  §13.1) oder den Beobachter (§13.2) — beide leben außerhalb des isolierten Kontexts.

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

## 10. Static Checks

Extends `spec/49_static_analysis.md`.  Errors unless stated otherwise.  F62-1…F62-5 (parameters),
F62-8/F62-9 (graph) and F62-13 (workspaces) are checks as well and are stated where they belong.

### 10.1 Structure

- **F62-16** `cache: resolved` requires a non-empty `resolve:`; `cache: pure` must not carry one.
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
- **F62-112** `step` ist **rein** (der Namensraum aus §13.5e/§5.7).  `step.arg` und `step.host`
  lesen genau das, was ohnehin im Schluessel steht — die **deklarierten** Achsen und Wirtswerte
  (§8); sie unrein zu nennen hiesse, die Schluesselvollstaendigkeit gegen sich selbst zu wenden.
  `step.workspace` ist aus Lane und Stufe abgeleitet und damit je Lane festgelegt (F62-107).
  `step.set_status`, `step.fail_lane` und `step.fail_pipeline` sagen etwas ueber den **Ausgang**,
  nicht ueber das Ergebnis, und ein Fehlschlag wird nicht aufgezeichnet (F62-92).  Aufgefallen
  beim Umschreiben der deklarierten Form: ihre erzeugten Rumpfe lesen ihre Achsen mit `step.arg`,
  und ein reiner Schritt waere damit unmoeglich geworden.

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
    parallel: auto
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
                args: []
                return: string           # the commit that was checked out
                resolve:
                  - decl: [argv, list<string>,
                           ["git", "ls-remote", "${REPO_URL}", "refs/heads/main"]]
                  - decl: [p, process, { call: [sys.exe, argv] }]
                  - if:
                      condition: { neq: [{ call: [p.wait] }, 0] }
                      then: [{ call: [fail_pipeline, "ls-remote fehlgeschlagen"] }]
                  - return: p.stdout_buffer
                body:
                  - decl: [argv, list<string>,
                           ["git", "clone", "--depth", "1", "${REPO_URL}", "src"]]
                  - decl: [p, process, { call: [sys.exe, argv] }]
                  - if:
                      condition: { neq: [{ call: [p.wait] }, 0] }
                      then: [{ call: [fail_pipeline, p.stderr_buffer] }]
                  - return: { call: [read_head, workspace] }

      - stage:
          name: compile
          needs: [sources]
          runs_on: "${arch}"             # the matrix axis picks the node
          args: [[arch, string], [compiler, string], [mode, string]]
          cache: pure
          on_fail: lane                  # one broken column must not stop the rest
          timeout: 3600000
          exec_defaults: { cwd: workspace, env: host_env, capture: true }
          host: [CC_WRAPPER]
          steps:
            - step:
                name: configure
                return: string           # the build directory
                body:
                  - decl: [bdir, string, "build-${arch}-${compiler}-${mode}"]
                  - decl: [argv, list<string>,
                           ["cmake", "-S", "src", "-B", bdir,
                            "-DCMAKE_C_COMPILER=${compiler}", "-DARCH=${arch}",
                            "-DCMAKE_BUILD_TYPE=${mode}"]]
                  - decl: [p, process, { call: [sys.exe, argv] }]
                  - if:
                      condition: { neq: [{ call: [p.wait] }, 0] }
                      then: [{ call: [fail_lane, p.stderr_buffer] }]
                  - return: bdir

            - step:
                name: build
                args: [[mode, string], [jobs, i32]]    # narrower than the stage
                return: BuildReport
                body:
                  - decl: [argv, list<string>, ["cmake", "--build", bdir, "-j", "${jobs}"]]
                  - decl: [o, ExeOptions]              # = the stage's exec_defaults
                  - set: [o.timeout, 1800000]
                  - decl: [p, process, { call: [sys.exe, argv, o] }]
                  - decl: [rc, i32, { call: [p.wait] }]
                  - if:
                      condition: { eq: [rc, 2] }
                      then: [{ call: [set_status, StepStatus.yellow] }]
                  - if:
                      condition: { gt: [rc, 2] }
                      then: [{ call: [fail_lane, "rc=${rc}: ${p.stderr_buffer}"] }]
                  - decl: [rep, BuildReport]
                  - set: [rep.artifact, "${bdir}/service"]
                  - set: [rep.warnings, { call: [count_warnings, p.stdout_buffer] }]
                  - return: rep

      - stage:
          name: publish
          per: once
          needs: [compile]               # receives list<BuildReport>, one per lane
          pause_before: true             # someone looks before this goes out
          host: [STATUS_URL]
          steps:
            - step:
                name: report
                args: []
                return: i32
                body:
                  - decl: [total, i32, 0]
                  - for: [rep, reports]
                    body: [{ set: [total, { +: [total, rep.warnings] }] }]
                  - call: [log.info, "Matrix fertig, ${total} Warnungen"]
                  - decl: [argv, list<string>, ["curl", "-fsS", "-X", "POST", "${STATUS_URL}"]]
                  - decl: [p, process, { call: [sys.exe, argv] }]
                  - return: { call: [p.wait] }
```

Six lanes (8 minus the two excluded).  Four things are worth reading off this example:

**The stage carries what the steps would repeat.**  `exec_defaults` gives every `sys.exe` below it
`cwd: workspace` and `env: host_env`, so `decl: [o, ExeOptions]` in `build` already holds them and
only `timeout` has to be set.  `configure` passes no options at all and still runs in the right
directory with the right environment — the correct thing is the default thing (F62-62).

**Reuse is finer than the matrix.**  The `compile` stage has `[arch, compiler, mode]` in scope;
`build` narrows to `[mode, jobs]`, because what it needs of `arch` and `compiler` is already inside
the `bdir` it receives, and that value reaches the key through `input_keys` (§8).  Raising `jobs`
therefore re-runs `build` and leaves every `configure` result in place.

**The expensive boundary is visible.**  `sources` and `publish` run wherever the run started;
`compile` runs on a node chosen by `${arch}`.  The two `needs` edges that cross a stage boundary
are exactly the two places where an output tree may have to travel (F62-48) — readable in the
source rather than inferred from resolved labels.

**Failure reach and status are explicit.**  `compile` declares `on_fail: lane`, so a broken column
leaves the other five running; `build` treats `rc == 2` as a tolerable outcome with its own status
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

- **F62-24** The local store sits in `.aiml_pipeline/` beside the pipeline file: `blobs/` for
  content plus a persistent KV for `step_key → result`.  Overridable with `AIML_PIPELINE_STORE`.
  Project-local rather than in the user's cache directory, because a pipeline belongs to a project
  and a cache one cannot find is a cache one cannot delete.

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
      - [workspace, string]               # je (Lane, Stage), F62-60
      - [state,     i32]                  # pending | running | paused | done (§5.9)
      - [status,    i32]                  # StepStatus, gültig ab done (§5.7)

- model:
    name: StepRun
    fields:
      - [name,        string]
      - [lane,        i32]                # Index in die Lane-Liste
      - [step_key,    string]             # §8
      - [output_tree, string]             # "" = nichts erzeugt
      - [reused,      i32]                # 1 = Treffer, Rumpf nicht gelaufen
      - [state,       i32]
      - [status,      i32]
      - [exit_code,   i32]
      - [started_ms,  i64]
      - [ended_ms,    i64]
```

- **F62-76** Diese Modelle sind die **einzige** Schnittstelle zwischen einem laufenden Lauf und
  seinem Wirt.  Ein Wirt liest sie; er baut sie nicht.  Was er damit tut — in eine Datenbank
  schreiben, über Mirror pushen, in einer Szene zeichnen — ist seine Sache (cosmos
  `doc/spec/96`/`97`/`98`).
- **F62-77** Sie sind **serialisierbar** (`spec/29`), aus demselben Grund wie ein Rückgabewert
  (F62-34): ein Lauf, der verteilt stattfindet, muss seinen Zustand über die Leitung geben können.

### 13.2 Wie der Wirt sie bekommt

- **F62-78** `run_<name>` nimmt einen optionalen **Beobachter**: eine Funktion, die bei jedem
  Zustandswechsel eines Schritts mit dem aktuellen `StepRun` gerufen wird.

  ```yaml
  - def:
      name: on_step
      args: [[run, ref<StepRun>]]          # ref<>, nicht StepRun und nicht ptr<>
      body: [ { call: [push_to_ui, run] } ]

  - decl: [rc, i32, { call: [pipe.run, on_step] }]
  ```

  **`ref<StepRun>`, und das ist keine Stilfrage.**  Gemessen: `StepRun` als Wert liest Müll (die
  Laufzeit übergibt einen Zeiger, die Signatur erwartet einen Wert), und `ptr<StepRun>` **stürzt
  ab** — `ptr<T>` bedeutet Besitz, also gibt der Empfänger beim Verlassen des Scopes einen Struct
  frei, der der Laufzeit gehört (`__libc_free` im Rückweg).  `ref<T>` ist die Leihgabe
  (`spec/61` §6.2) und das einzig Richtige.

  Ein Beobachter statt eines Rückgabewerts, weil der Wirt **während** des Laufs reagieren muss —
  anhalten, anzeigen, abbrechen.  Ein Ergebnis am Ende käme für alles davon zu spät.
- **F62-79** Ohne Beobachter läuft die Pipeline ebenso; der lokale JIT-Lauf (§12) braucht keinen.
  Der Beobachter ist eine Erweiterung, keine Voraussetzung.
- **F62-80** Der Beobachter darf den Lauf **nicht** verändern.  Technisch bekommt er ein
  `ref<StepRun>` (siehe oben — ein Wert liest Müll, ein `ptr<>` führt zu einer fremden Freigabe),
  aber der Wert lebt **nur für die Dauer des Aufrufs**: er wird je Ereignis neu gebaut.  Wer
  hineinschreibt, ändert nichts — und genau das ist gemeint.  Steuerung läuft über die Wirtsseite
  (anhalten/fortsetzen/abbrechen, cosmos `doc/spec/96` §96.11), nicht über ein Modell.

### 13.3 Zwei Formen: deklariert und zusammengesetzt

Es gibt **zwei** Wege zu einer Pipeline, und sie unterscheiden sich nicht im Ergebnis, sondern
darin, wann die Struktur feststeht.

| | **deklariert** (`- pipeline:`) | **zusammengesetzt** (`pipeline`-Typ, §13.5) |
|---|---|---|
| Struktur | Quelltext | zur Laufzeit gebaut |
| Matrix | aufgeklappt beim Übersetzen (§4) | aufgeklappt beim Start des Laufs |
| Struktur-Prüfungen (§10.1) | Compile-Fehler | Fehler beim Start, **bevor** ein Schritt läuft |
| Rumpf-Prüfungen (§10.2, Purity) | Compile-Fehler | **ebenso Compile-Fehler** — der Rumpf ist ein `Code`-Bundle |
| Schlüssel (§8) | Hash des geschriebenen Rumpfs | Hash des Bundle-Inhalts (`AimlCode.id`) |

**Der Punkt, an dem die Grenze verläuft, ist der Rumpf, nicht die Struktur.**  Ein
zusammengesetzter Schritt bekommt seinen Rumpf als `Code` (`sys.bundle`), also als übersetzten,
geprüften Quelltext mit Inhaltsadresse.  Deshalb bleiben die beiden Eigenschaften erhalten, auf
die es ankommt: die Reinheitsprüfung greift (sie prüft den Rumpf) und der Cache-Schlüssel ist
inhaltsabgeleitet (er hasht den Rumpf).

Was wirklich verloren geht, ist eines: **die Struktur-Prüfung wandert von der Bauzeit zum
Laufbeginn.**  Ein Tippfehler in einem Parameternamen, ein `resolved` ohne Sonde, ein `exclude`
auf eine Achse, die es nicht gibt — das meldet der deklarierte Weg beim Übersetzen, der
zusammengesetzte beim Start.  Früher wäre besser; "bevor irgendein Schritt läuft" ist die
nächstbeste Stelle und deutlich besser als mitten im Lauf.

- **F62-81** Beide Formen münden in **dieselbe** Ausführung und dieselben Laufzeit-Modelle
  (§13.1).  Es gibt keinen zweiten Executor und keinen laxeren Pfad; ein zusammengesetzter Lauf
  wird genauso gecacht, beobachtet und gesteuert wie ein deklarierter.
- **F62-84** Eine zusammengesetzte Pipeline wird beim Start **vollständig geprüft** und schlägt
  fehl, bevor ein Schritt läuft.  Teilweise ausgeführte Läufe aus einer fehlerhaften Struktur sind
  ausgeschlossen — sie hinterließen Cache-Einträge für eine Pipeline, die es so nie gab.

### 13.5 Die Typen `pipeline`, `stage`, `step`

```yaml
- decl: [bld, step, { call: [step.new] }]
- call: [bld.set_name, "build"]
- call: [bld.set_body, { call: [sys.bundle, "do_build", { context: true }] }]   # Code
- call: [bld.set_cache, CacheClass.pure]
- call: [bld.use_arg, "mode"]                              # nur deklarierte Achsen (§5.2)

- decl: [cmp, stage, { call: [stage.new] }]
- call: [cmp.set_name, "compile"]
- call: [cmp.set_runs_on, "${arch}"]
- call: [cmp.add_step, bld]

- decl: [pipe, pipeline, { call: [pipeline.new] }]
- call: [pipe.set_name, "generated"]
- call: [pipe.add_param, "arch", arch_values]              # list<string> zur Laufzeit
- call: [pipe.add_stage, cmp]
- decl: [rc, i32, { call: [pipe.run] }]                    # optional: pipe.run(on_step)
```

#### 13.5a `on_fail`, `timeout`, `retry` und der Status in der zusammengesetzten Form

```yaml
- call: [bld.set_on_fail, OnFail.pipeline]      # §5.6, Vorgabe: OnFail.lane
- call: [bld.set_timeout, 60000]                # ms, ganzer Schritt (F62-40); 0 = keiner
- call: [bld.set_retry, 3]                      # Versuche (F62-41), Vorgabe 1
- decl: [st, i32, { call: [bld.status] }]       # StepStatus, gueltig ab done (§13.1)
- decl: [lst, i32, { call: [pipe.status] }]     # aggregiert ueber die Lanes (F62-43)
```

- **F62-90** Die drei Funktionen aus §5.7 (`set_status`, `fail_lane`, `fail_pipeline`) sind in der
  **deklarierten** Form generiert (F62-45), in der **zusammengesetzten** Form dagegen eingebaute
  Funktionen im Namensraum `step`: `step.set_status`, `step.fail_lane`, `step.fail_pipeline`.  Sie
  schreiben auf einen laufzeitseitigen Platz fuer den *gerade laufenden Schritt* — thread-lokal,
  denn Lanes duerfen in Threads laufen (§6).
  Der Namensraum ist kein Schoenheitsfehler, sondern erzwungen, und das ist beim Implementieren
  aufgefallen: ein Schritt-Rumpf ist in dieser Form eine **gewoehnliche Funktion**, die
  `sys.bundle` nur *zusaetzlich* einsammelt — sie wird also auch im gewoehnlichen Kontext
  mituebersetzt.  Ein Name, der "nur im Schritt-Rumpf" existiert, scheitert genau dort mit
  `Unknown function`.  Darum ein Name, der ueberall aufloest, dessen **Wirkung** aber an einen
  laufenden Schritt gebunden ist: ohne laufenden Schritt ist der Aufruf ein Fehler und kein
  stiller Nulleffekt.  Dass die deklarierte Form die blossen Namen behalten kann (F62-45), bleibt
  richtig — dort generiert der Compiler den Rumpf samt Praeambel und weiss, dass es ein Rumpf ist.
- **F62-94** Der dritte Wert von `on_fail` heisst in der zusammengesetzten Form `OnFail.proceed`,
  nicht `OnFail.continue`: `continue` ist in aiml ein Schluesselwort (Schleifen), und ein
  Enum-Glied kann nicht so heissen.  In der **deklarierten** Form bleibt es beim Attributwert
  `continue` aus §5.6 — dort ist es Text in einer Abbildung und kollidiert mit nichts.  Derselbe
  Begriff, zwei Schreibweisen, weil die eine Stelle Grammatik ist und die andere nicht.
- **F62-91** *(F62-64-Pflicht: Schluesselmaterial?)*  `on_fail`, `timeout` und `retry` sind
  **kein** Schluesselmaterial — alle drei beschreiben, was bei einem **Fehlschlag** geschieht, und
  aufgezeichnet wird ausschliesslich ein **gelungener** Versuch (§7, F62-41).  Zwei Schritte, die
  sich nur in diesen Feldern unterscheiden, liefern bei Erfolg dasselbe Ergebnis; sie zu trennen
  wuerde Wiederverwendung ohne Gegenwert kosten.  Gegenprobe: `timeout` ist deshalb kein
  Schluesselmaterial, weil ein Ablauf den Schritt **scheitern** laesst (F62-40) und ein
  gescheiterter Schritt nichts aufzeichnet — nicht, weil die Zeit unwichtig waere.
- **F62-92** `fail_lane` und `fail_pipeline` verhindern die Aufzeichnung; ein blosses `set_status`
  verhindert sie **nicht**.  Grund: welche Farben "Fehlschlag" bedeuten, ist offen (§16 Punkt 6) —
  die Aufzeichnung an eine offene Bedeutung zu binden, hiesse eine Entscheidung zu treffen, die
  hier ausdruecklich nicht getroffen ist.  Wer einen Lauf nicht aufgezeichnet haben will, sagt es
  mit `fail_lane`, und das heisst es unabhaengig von jeder Farbdeutung.
#### 13.5g `per:` und `needs:` — Reihenfolge, Fehlerreichweite, Ergebnisse

```yaml
- call: [clone.set_per, Per.once]            # §5.5: einmal fuer die ganze Matrix
- call: [build.add_need, "clone"]            # §5.1/F62-9: nur fruehere Schritte
```

Die Trennung ist nicht Bequemlichkeit, sondern eine **gemessene Grenze**: `aiml_exec_code` gibt
einen **Status** zurueck, nicht den Wert des Rumpfs (`tests/test_bundle_exec_semantics.aiml`:
`return 7` im Bundle ergibt `rc=0`).  In der zusammengesetzten Form ist der Rueckgabewert eines
Schritts damit **nicht abholbar**, und F62-33 (die Werte der `needs` als weitere Argumente) ist
nicht etwa unfertig, sondern durch die heutige ABI ausgeschlossen.

- **F62-113** In der zusammengesetzten Form bedeutet `needs` **Reihenfolge, Fehlerreichweite und
  Ergebniszugriff** (§13.5h) — aber nicht Argumentuebergabe: der Rueckgabewert eines Rumpfs
  erreicht den Nachfolger nicht als Argument, sondern ueber den Ergebniskanal, und wer ihn als
  Argument erwartet, bekommt nichts.  Daneben bleibt der **Arbeitsbereich** (§4.3) der Weg fuer
  alles, was keine Zeichenkette ist: Dateien.
- **F62-114** *(F62-64-Pflicht: Schluesselmaterial?)*  `needs` **ist** Schluesselmaterial, und
  zwar ueber `input_keys` (§8): der Schluessel eines Schritts enthaelt die Schluessel seiner
  `needs`.  Damit wirkt eine Aenderung weiter oben transitiv nach unten — das ist, was eine Kette
  von Schritten erst wie ein Stapel von Schichten verhalten laesst.  Gegenprobe: liesse man es
  weg, traefe ein Nachfolger seinen alten Eintrag, obwohl sein Vorgaenger neu gelaufen ist, und
  der Arbeitsbereich unter ihm waere ein anderer.
- **F62-115** *(F62-64-Pflicht)*  `per` ist **kein** Schluesselmaterial, begrenzt aber, was im
  Schluessel stehen DARF: ein `per: once`-Schritt hat **keine** Lane-Bindungen, weil es ein
  Ergebnis fuer die ganze Matrix gibt.  Ein `per: once` mit deklarierten Achsen (`use_arg`) ist
  darum ein **Fehler** und nicht etwa "nimmt die erste Lane" — letzteres waere ein Ergebnis, das
  von der Reihenfolge der Lanes abhaengt.
- **F62-116** Die Ausfuehrung wird an jedem `per: once`-Schritt zu einem **Riegel**: alle Lanes
  erreichen ihn, er laeuft genau einmal, danach laufen die Lanes weiter.  Zwischen zwei Riegeln
  bleibt es bei Lane-Parallelitaet (§6) — der Riegel ist die einzige Stelle, an der gewartet wird,
  weil er die einzige Stelle ist, an der etwas Gemeinsames entsteht.
#### 13.5h Der Ergebniskanal — Datenfluss ohne Aenderung der JIT-ABI

F62-113 hielt fest, dass der Rueckgabewert eines Rumpfs nicht abholbar ist: der erzeugte
Einsprungpunkt ruft die Startfunktion und verwirft ihr Ergebnis, und `aiml_exec_code` liefert
einen **Status**.  Daraus folgt aber **nicht**, dass es keinen Datenfluss geben kann — nur, dass er
nicht ueber den Rueckgabewert laufen muss.  Ein Rumpf laeuft im selben Prozess wie die Laufzeit;
er kann sein Ergebnis **veroeffentlichen**, genauso wie er schon seinen Status setzt (F62-90).

```yaml
          body:
            - call: [step.set_result, head_commit]      # veroeffentlichen
            - return: 0
          # im Nachfolger, der `clone` in needs hat:
            - decl: [commit, string, { call: [step.need_result, "clone"] }]
```

- **F62-118** Ergebnisse sind **Zeichenketten** — die serialisierte Form (F62-34).  `step.set_result`
  nimmt auch eine Zahl und schreibt sie dezimal; gelesen wird immer eine Zeichenkette.  Grund: der
  Wert muss neben dem Schluessel **aufgezeichnet** werden koennen, und was aufgezeichnet wird, ist
  serialisiert.  Ein Modell geht ueber `sys.serialize`/`sys.deserialize` (`spec/29`) durch denselben
  Kanal; dass das heute der Autor schreibt und nicht der Compiler, ist der Rest der Luecke.
- **F62-119** `step.need_result(name)` ist ein **Fehler**, wenn `name` nicht in den `needs` des
  rufenden Schritts steht — nicht eine leere Zeichenkette.  Dieselbe Begruendung wie bei F62-106:
  ein Ergebnis, das auf den Rumpf wirkt, ohne ueber `needs` im Schluessel zu stehen (`input_keys`,
  F62-114), waere ein **blinder Schluessel**.  Der Kanal ist damit nicht nur bequem, sondern genau
  so weit geoeffnet, wie der Schluessel reicht.
- **F62-120** Ein Treffer stellt das Ergebnis **wieder her**, ohne den Rumpf zu rufen (F62-37).
  Der Cache haelt deshalb `(Status, Ergebnis)` und nicht nur den Status.  Ohne das waere ein
  wiederverwendeter Schritt fuer seine eigenen Nachfolger unsichtbar — sie bekaemen nichts und
  muessten neu laufen, was die Wiederverwendung dort wieder aufheben wuerde, wo sie am meisten
  wert ist.
- **F62-121** In der **deklarierten** Form veroeffentlicht der Compiler: aus
  `return: <typ>` wird ein Huelle-Rumpf, der den geschriebenen Rumpf ruft und sein Ergebnis in den
  Kanal legt.  Der Autor schreibt also weiterhin `- return: rep` (§5.3) und muss von `set_result`
  nichts wissen; F62-33 ist damit in der deklarierten Form **erfuellt**, soweit der Typ eine
  Zeichenkette oder eine Zahl ist.  Umgekehrt erscheinen die Ergebnisse der `needs` als **lokale
  Namen gleich dem Schrittnamen** (wie die Achsen, F62-109) — darum schreibt man `${clone}` und
  meint, was `clone` veroeffentlicht hat.  Ein Name, der gleichzeitig Achse und `needs` ist, waere
  im Rumpf zweimal belegt und ist ein **Fehler**, keine Vorrangregel.

- **F62-117** Scheitert ein `per: once`-Schritt mit `on_fail: lane`, werden genau die Schritte
  **uebersprungen**, die ihn (transitiv) brauchen — sie starten nicht.  Schritte, die ihn nicht
  brauchen, laufen weiter.  Die Reichweite folgt damit dem Graphen und nicht einer Vermutung
  (F62-39).  Ein uebersprungener Schritt ist nicht gescheitert: er hat keinen Ausgang und wird
  nicht aufgezeichnet.

#### 13.5e Wie ein Rumpf seine Lane sieht

In der deklarierten Form bekommt ein Rumpf die deklarierten Achsen als **Argumente**, dazu
`workspace` und `host_env` (§5.2).  In der zusammengesetzten Form ist der Rumpf ein Bundle, das
ohne Argumente gerufen wird — `use_arg` erklaert bisher nur, was in den **Schluessel** geht.  Damit
waere die Matrix wirkungslos: jede Lane fuehrte denselben Rumpf aus, ohne zu wissen, welche Lane
sie ist.  Aufgefallen beim Implementieren von §10.

- **F62-105** Der Rumpf liest seine Lane über den Namensraum `step`:

  | Aufruf | Ergebnis |
  |---|---|
  | `step.arg("arch")` | der Wert dieser Achse in dieser Lane |
  | `step.workspace()` | absoluter Pfad des Arbeitsbereichs dieser (Lane, Stufe) (§4.3) |
  | `step.host("PKG")` | der Wirtswert dieses Namens, sonst leer |

  Gelesen wird aus einem thread-lokalen Platz, den die Laufzeit vor dem Rumpf setzt — dieselbe
  Bauweise wie `step.set_status` (F62-90) und aus demselben Grund: ein Bundle traegt keinen
  Zustand des Aufrufers (F62-89).
- **F62-106** `step.arg(name)` ist ein **Fehler**, wenn die Achse nicht mit `use_arg` deklariert
  wurde — nicht ein leerer Wert.  Begruendung: eine Achse, die der Rumpf liest, aber nicht
  deklariert, waere ein **blinder Schluessel**: der Wert wirkt auf das Ergebnis, steht aber nicht
  im Schluessel, und zwei verschiedene Lanes teilten einen Eintrag.  Das ist genau das Loch, das
  §1.1 beschreibt; ein leerer Wert wuerde es verstecken.  Das Gegenstueck zu F62-7, nur zur
  Laufzeit statt im Quelltext, weil in dieser Form erst die Laufzeit die Deklaration kennt.
- **F62-107** *(F62-64-Pflicht: Schluesselmaterial?)*  Der Arbeitsbereichs-Pfad ist **kein**
  Schluesselmaterial, obwohl der Rumpf ihn liest.  Er enthaelt Lane und Stufe, die schon ueber die
  Bindungen im Schluessel stehen (§8), und zusaetzlich das Wurzelverzeichnis — naehme man es
  hinein, ergaebe dasselbe Projekt an einem anderen Ort andere Schluessel, und kein Treffer
  ueberlebte einen Umzug.  Dass der Pfad im Ergebnis auftauchen KANN (etwa in einer Logzeile), ist
  ein Grund fuer `opaque`, nicht fuer ein neues Schluesselfeld.

#### 13.5d Wie §10 die zusammengesetzte Form erreicht

§10 prueft **statisch**, und `set_cache(CacheClass.pure)` steht in der zusammengesetzten Form erst
zur **Laufzeit** fest.  Damit waere die Reinheitspruefung (§10.2) auf die deklarierte Form
beschraenkt — also ausgerechnet dort weg, wo Pipelines aus einer Datenbank oder einem Generator
entstehen.

- **F62-102** Die Reinheit wird beim **Buendeln** erklaert, nicht beim Setzen der Klasse:
  `sys.bundle("do_build", { context: true, pure: true })`.  Das ist die Stelle, an der der
  Compiler den Rumpf in der Hand hat, und es ist dieselbe Stelle, an der schon die
  Vollstaendigkeit geprueft wird (`spec/29_2` §18).  `pure: true` loest genau die Pruefungen aus
  §10.2 aus; ohne die Angabe prueft niemand etwas, und `set_cache(pure)` ist dann eine Behauptung
  ohne Deckung.
- **F62-103** Ein Schritt mit `cache: pure`, dessen Rumpf **nicht** mit `pure: true` gebuendelt
  wurde, ist ein **Warnfall** zur Laufzeit, kein Fehler.  Begruendung: die Laufzeit kann die
  Behauptung nicht nachpruefen (der Rumpf ist ein Bundle), und einen Lauf abzubrechen, weil eine
  *Pruefung* fehlt, wuerde die Pipeline zum Geisel einer Angabe machen, die der Autor vielleicht
  bewusst nicht gemacht hat.  Die Warnung sagt genau das: hier gilt `pure` unbewiesen.
- **F62-104** `assert_pure` (F62-22) gehoert aus demselben Grund an dieselbe Stelle:
  `sys.bundle("do_build", { context: true, pure: true, assert_pure: true })`.  Ohne
  `assert_pure` ist ein `sys.exe` im Rumpf eines `pure: true`-Bundles ein **Fehler** — nicht,
  weil `sys.exe` schlecht waere, sondern weil die Unterschrift fehlt, die ein Mensch leisten muss
  (der Compiler kann nicht in ein Programm sehen).

#### 13.5c `parallel` in der zusammengesetzten Form

```yaml
- call: [pipe.set_parallel, 8]      # hoechstens 8 Lanes gleichzeitig; 0 = auto (ein Kern je Lane)
```

- **F62-98** `parallel` begrenzt gleichzeitige **Lanes** (F62-10) und ist **kein**
  Schluesselmaterial (F62-64-Pflicht): es entscheidet, wie viele Lanes gleichzeitig laufen, nicht
  was eine Lane erzeugt.  Zwei Laeufe derselben Pipeline mit `parallel: 1` und `parallel: 8`
  muessen dieselben Schluessel ergeben — waere es anders, waere die Nebenlaeufigkeit selbst eine
  Eingabe, und ein Treffer haenge davon ab, wie schnell die Maschine war.
- **F62-99** Der Schritt-Rumpf ist in dieser Form ein Bundle, das zur Laufzeit **uebersetzt** wird
  (§13).  Die Laufzeit serialisiert genau diesen Abschnitt — die Lanes laufen parallel, das
  Uebersetzen eines Rumpfs nicht.  Das ist die Rueckfallvariante aus F62-14, angewandt auf den
  Uebersetzer statt auf den Prozessstart.

  **Gemessen (2026-10-12, `tests/test_concurrent_isolated_exec.aiml`):** 24 isolierte
  Uebersetzungen, einmal aus **8 Threads** und einmal **einthreadig**, dieselbe Arbeit:

  | Form | Dauer (3 Laeufe) |
  |---|---|
  | 8 Threads | 1040 / 1026 / 1032 ms |
  | einthreadig | 958 / 967 / 964 ms |

  Gleichzeitigkeit bringt **nichts** — sie ist sogar leicht langsamer, und 240 Uebersetzungen ueber
  10 Laeufe liefen fehlerfrei durch.  Der Durchsatz ist von der Threadzahl **unabhaengig**
  (~40 ms je Uebersetzung), obwohl im isolierten Pfad kein eigenes Schloss liegt: irgendwo in
  LLVM wird ohnehin serialisiert.
  **Folge:** das Schloss bleibt.  Es kostet messbar nichts und nimmt eine ganze Fehlerklasse aus
  dem Spiel — das ist ein besserer Handel als eine Nebenlaeufigkeit, die keinen Gewinn bringt.
  Die Frage ist damit **beantwortet**, nicht offen: wer sie neu stellt, muss zuerst zeigen, dass
  der Durchsatz ueberhaupt an der Threadzahl haengt.

  **⚠️ Nachtrag 17.10.2026 — die Messung oben ist nicht mehr vollstaendig.** Derselbe Test
  (`tests/test_concurrent_isolated_exec.aiml`) ist im vollen Korpuslauf vom 16.10.2026
  **mit SIGSEGV abgestuerzt** (`--clean`-Vollbau, einziger Fehlschlag ausserhalb der zwei
  spec-vor-Code-Labore).  Danach lief er im naechsten vollen Lauf und in **sechs** aufeinander
  folgenden Einzellaeufen fehlerfrei.  Der Fehler ist also **sporadisch und lastabhaengig** — die
  10 Laeufe von 2026-10-12 haben ihn nicht getroffen, was bei dieser Trefferrate erwartbar ist.

  Das aendert die **Folge** nicht, es verstaerkt sie: das Schloss bleibt, und zwar jetzt nicht nur
  weil Nebenlaeufigkeit nichts bringt, sondern weil der isolierte Pfad unter Last nachweislich
  nicht sicher ist.  Was sich aendert, ist der Satz „liefen fehlerfrei durch" — er gilt fuer jene
  10 Laeufe, nicht als Aussage ueber den Pfad.  Wer die Frage neu stellt, muss ausserdem diesen
  Absturz erklaeren.  Querverweis: `spec/65` F65-25 (ein Prozess je Nutzer) stuetzt sich darauf.
- **F62-100** Der Beobachter (§13.2) wird **serialisiert** gerufen, auch wenn Lanes parallel
  laufen.  Er ist benutzergeschriebener aiml-Code im Kontext des Aufrufers (F62-89); ihn aus acht
  Threads gleichzeitig zu rufen hiesse, jeden Beobachter ohne Vorwarnung thread-sicher zu machen.
  Die Reihenfolge der Aufrufe ist dann nicht die Quelltextreihenfolge — das ist bei parallelen
  Lanes unvermeidlich und steht hier, damit niemand sich darauf verlaesst.

#### 13.5b Anhalten und Fortsetzen in der zusammengesetzten Form

```yaml
- call: [dep.set_pause_before, 1]            # §5.9, deklariertes Tor (F62-54)
- call: [pipe.request_pause]                 # von aussen, wirkt NACH dem Schritt (F62-55)
- call: [pipe.release, "deploy"]             # ein deklariertes Tor freigeben
- call: [pipe.resume]                        # weiter; der Rest kommt aus dem Cache (F62-56)
- decl: [lc, i32, { call: [pipe.lifecycle] }]  # Lifecycle.pending|running|paused|done
```

- **F62-95** `pipeline.run()` liefert `-6`, wenn der Lauf **angehalten** wurde.  Nicht `0`: ein
  angehaltener Lauf ist nicht fertig, und `0` hiesse "durch".  Nicht die Farbe eines Schritts:
  `paused` ist Lebenszyklus, kein Ausgang (F62-44 ergaenzt).  Nach einem `resume` ist ein weiterer
  `run()` buchstaeblich dasselbe wie ein zweiter Lauf derselben Pipeline — die bereits
  aufgezeichneten Schritte treffen, der angehaltene laeuft.  Damit ist F62-56 nicht
  implementiert, sondern **nicht noetig**: Fortsetzen braucht keinen eigenen Zustand.
- **F62-96** *(F62-64-Pflicht: Schluesselmaterial?)*  `pause_before` ist **kein**
  Schluesselmaterial.  Es entscheidet, **wann** ein Schritt laeuft, nicht **was** er erzeugt —
  derselbe Schritt mit und ohne Tor liefert dasselbe Ergebnis, und ein Tor in den Schluessel zu
  nehmen hiesse, jede Freigabe als neues Ergebnis zu zaehlen.  Gegenprobe: wuerde ein Tor den
  Inhalt beeinflussen (etwa weil jemand im Wartezustand den Arbeitsbereich veraendert), waere
  nicht das Tor das Schluesselmaterial, sondern die Veraenderung — und die faellt unter §7
  (`opaque`), nicht unter dieses Feld.
- **F62-97** Von aussen angehalten wird in dieser Form aus dem **Beobachter** (§13.2): er laeuft
  je (Schritt, Lane) im Kontext des Aufrufers (F62-89), also genau dort, wo eine Bedienoberflaeche
  oder ein Dienst sitzt.  `request_pause` aus einem Schritt-Rumpf wirkt ebenfalls, denn der Platz
  liegt an der Pipeline und nicht im isolierten Kontext.

- **F62-93** Die Praezedenzordnung aus F62-43 steht in der Implementierung an **einer** Stelle als
  Tabelle (`kStatusPrecedence`, `pipeline_runtime.cpp`) und ist ausdruecklich **vorlaeufig**, bis
  §16 Punkt 6 entschieden ist.  Tests pruefen deshalb nur, was schon entschieden ist: dass die
  Ordnung **total** ist, dass aggregiert statt gemischt wird (F62-43), und dass die Farbe eines
  Schritts die Lane erreicht — keine einzelne Farbbedeutung.

Methoden statt Feldzuweisung, und das ist keine Geschmacksfrage: eingebaute Typen sind in aiml
**opake Handles** (`Code`, `process`, `path`).  `set: [bld.name, …]` setzte einen Struct-Member
voraus — und ein Struct kann die variable Struktur (Schrittliste, Achsenliste) nicht tragen.  Der
Wert ist ein Griff auf etwas, das wächst.

- **F62-85** `step.body` ist ein **`Code`**, kein String und kein Funktionszeiger.  Ein String
  wäre ungeprüfter Text; ein Funktionszeiger hätte keine Inhaltsadresse.  `Code` ist beides:
  übersetzt und gehasht.
- **F62-86** `use_arg(name)` entspricht `args:` der deklarierten Form (§5.2) — es sagt, welche
  Achsen dieser Schritt benutzt, und **nur diese** gehen in seinen Schlüssel.  Ohne Angabe erbt er
  die Achsen seiner Stufe; das ist die bequeme und die teurere Variante, genau wie dort.
- **F62-87** `add_step` und `add_stage` **kopieren** den übergebenen Wert hinein.  Ein danach
  geänderter `step` wirkt nicht mehr auf die Stufe, in der er schon steckt — sonst könnte eine
  Pipeline ihre Form ändern, nachdem ihre Schlüssel gebildet wurden.  Wer zwei ähnliche Schritte
  will, baut zwei und fügt beide ein.
- **F62-88** `pipeline.run()` ist in beiden Formen dasselbe: dieselbe Lane-Expansion, dieselbe
  Schlüsselbildung, dieselben Beobachter.  Der deklarierte Weg baut dieselbe Struktur zur
  Übersetzungszeit auf, statt sie zur Laufzeit entgegenzunehmen.

### 13.5f Die deklarierte Form ist Zucker auf der zusammengesetzten

Die deklarierte Form (§5) wird **vor** dem Uebersetzen in die zusammengesetzte Form umgeschrieben.
Sie ist damit keine zweite Implementierung, sondern eine Schreibweise — und genau das verlangt
F62-23 (der Compiler erzeugt den Ausfuehrer) ohne zwei Ausfuehrer zu bekommen, die auseinander
driften (§16 Punkt 4).

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

wird zu drei gewoehnlichen Deklarationen: einer Funktion fuer den Rumpf, optional einer fuer die
Sonde, und einer Funktion `demo()`, die `pipeline`/`stage`/`step` zusammensetzt und die Pipeline
zurueckgibt.

- **F62-108** Eine deklarierte Pipeline `name: demo` erzeugt eine Funktion `demo()` mit
  Rueckgabetyp `pipeline`.  Der Name muss ein gueltiger aiml-Bezeichner sein.  Gebaut wird sie
  also erst, wenn jemand sie ruft — die Deklaration selbst fuehrt nichts aus, und `demo().run()`
  ist dasselbe wie eine handgeschriebene zusammengesetzte Pipeline.
- **F62-109** Die deklarierten Achsen erscheinen im Rumpf als **lokale Variablen** gleichen
  Namens, erzeugt aus `step.arg(...)` (§13.5e); dazu `workspace` und je ein Name aus `host:`.
  Deshalb schreibt man `${arch}` im Rumpf und meint die Achse.
- **F62-110** **F62-7 braucht damit keinen eigenen Pruefdurchlauf**: ein `${NAME}`, das keine
  deklarierte Achse und kein `host:`-Name ist, ist im erzeugten Rumpf ein unbekannter Name und
  damit ein **Uebersetzungsfehler** — der Fehler, den F62-7 verlangt, faellt aus der Umschreibung
  heraus.  Das ist besser als eine zweite Pruefung, die mit der ersten uneins werden kann: wer
  eine Achse vergisst, bekommt sie an der Stelle gemeldet, an der sie benutzt wird.
  **Gemessen** (`tests/pipeline_declared_neg/undeclared_axis.aiml`): der Fehler kommt, die
  *Meldung* ist aber unscharf — `Unknown Type: 'mode'` statt "unbekannter Name", weil die
  Namensauflösung in dieser Lage in die Typauflösung fällt.  Das ist ein eigener Mangel
  (verwechselt Name und Typ) und keine Eigenschaft von F62-7; er gehoert behoben, ohne dass die
  Aussage hier davon abhaengt.
- **F62-111** Was die Umschreibung **noch nicht** abdeckt und darum auch nicht vorgibt zu koennen:
  `outputs:` (§16 Punkt 5), `runs_on` (§5.8) und `per`/`needs` **auf einer Stufe** (sie gelten nur
  am Schritt).  Diese Felder werden beim Umschreiben **abgelehnt**, nicht ignoriert — ein
  stillschweigend verworfenes `needs:` waere eine Pipeline, die in falscher Reihenfolge laeuft und
  dabei gruen meldet.  `per:` und `needs:` am **Schritt** werden getragen (§13.5g), der
  **Datenfluss** der `needs`-Werte (F62-33) dagegen nicht — siehe F62-113.

### 13.6 Pipelines als Quelltext erzeugen — der dritte Weg

Neben der deklarierten (§13.3) und der zusammengesetzten Form (§13.5) gibt es einen dritten Weg:
**Quelltext erzeugen** und regulär übersetzen.  Er lohnt sich, wo die Struktur aus Daten entsteht
und man die Prüfungen trotzdem zur **Bauzeit** haben will — der zusammengesetzte Weg verschiebt sie
auf den Laufbeginn (§13.3), dieser hier nicht.

```yaml
- def:
    name: build_pipeline_source
    args: [[rows, list<StageRow>]]
    return: { type: string, lifetime: producer }
    body:
      - decl: [src, string, "- pipeline:\n    name: generated\n    steps:\n"]
      - for: [row, rows]
        body:
          - set: [src, "${src}      - stage:\n          name: ${row.name}\n"]
          - set: [src, "${src}          runs_on: \"${row.label}\"\n          steps:\n"]
          - set: [src, "${src}            - step:\n                name: run\n"]
          - set: [src, "${src}                body: [...]\n"]
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

Warum dieser Umweg besser ist als ein zusammensetzbarer Typ:

- **Die Prüfungen laufen.**  Der erzeugte Text geht durch denselben Compiler wie ein geschriebener:
  `resolved` ohne `resolve:` ist ein Fehler, ein unbekanntes Attribut ist ein Fehler (F62-65), ein
  Tippfehler in einem Parameternamen ist ein Fehler.  Ein Generator, der Unsinn erzeugt, erfährt
  es — und zwar bevor etwas läuft.
- **Der Schlüssel stimmt.**  Gehasht wird der **erzeugte** Text (§8).  Zwei Läufe, deren Generator
  dasselbe erzeugt, treffen denselben Cache-Eintrag; ändert sich eine Zeile, trifft nur der
  betroffene Schritt nicht mehr.  Ein zur Laufzeit zusammengesetztes Objekt hätte keinen
  Quelltext und damit keinen stabilen Schlüssel.
- **Die Matrix funktioniert.**  Der Generator darf `params:` schreiben; aufgeklappt wird sie beim
  Übersetzen des erzeugten Textes, nicht vom Generator.  Wer die Lanes selbst ausschreiben müsste,
  hätte den Vorteil verloren.

- **F62-82** Erzeugter Pipeline-Quelltext ist gewöhnlicher Quelltext.  Es gibt keinen zweiten,
  laxeren Pfad für ihn — dieselben Prüfungen, dieselbe Schlüsselbildung, dieselben Fehler.
- **F62-83** Der Generator soll den erzeugten Text **ablegen können**, bevor er ihn ausführt
  (`io.write`), denn ein Fehler in erzeugtem Text ist ohne den Text nicht zu lesen.  Eine
  Fehlermeldung mit `zeile:spalte` (spec/49 `--check`) zeigt auf eine Zeile, die sonst nirgends
  steht.

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
5. **`outputs:` per step.**  Capturing the whole lane workspace is always correct but transfers
   more than needed; globs are cheaper and can miss a file a later step needs.  Not in this draft.
6. **What the six statuses mean, and their precedence order.**  F62-42 fixes that there are six
   and what they are called; the meanings are open, and the aggregation order of F62-43 follows
   from them.  The obvious reading treats the three primaries as facets that combine — failure,
   success, and "did not run" — which would make the mixtures "succeeded without running" (a cache
   hit), "failed without running" (a replayed failure) and "failure and success at once".  That
   reading is consistent and is *not* adopted here, because the naming should be settled once
   rather than inherited from a draft.  What is already decided: six values, mixture names, a
   total precedence order, no additive mixing for aggregates (F62-43), and outcome kept separate
   from lifecycle (F62-44).

## 17. `runtime:` — geteilter oder eigener Ausführungskontext

Eine Pipeline sagt, **worin** ihre Schritte laufen:

```yaml
- pipeline:
    name: build_matrix
    runtime: shared | own          # Vorgabe: shared
```

| Wert | Bedeutung |
|---|---|
| `shared` | der Kontext des Wirts.  Bibliotheken, die der Wirt geladen hat, sind sichtbar; nichts wird dafür erneut übersetzt |
| `own` | ein eigener Kontext mit **eigenem Symbolraum**.  Die Pipeline bringt mit, was sie braucht, und sieht nichts vom Wirt |

- **F62-72** `runtime: own` ist nötig, sobald zwei Pipelines **verschiedene Versionen derselben
  Bibliothek** brauchen.  Mit einem geteilten Symbolraum gibt es pro Name genau einen Eintrag —
  eine Version gewinnt, die andere verschwindet, **ohne Fehlermeldung**.  Das ist dieselbe
  Fehlerklasse wie ein zu grober Cache-Schlüssel (F62-64): es läuft, und es läuft falsch.
- **F62-73** `runtime` ist **Schlüsselmaterial** (F62-64, Fall 2).  Derselbe Schritt in einem
  anderen Kontext kann gegen andere Bibliotheksversionen laufen und etwas anderes erzeugen; ein
  gemeinsamer Cache-Eintrag wäre ein falscher Treffer.
- **F62-74** Welche Bibliotheken eine Pipeline braucht, steht bei ihr, nicht beim Wirt:

  ```yaml
    libraries:
      - { name: acme_build, version: "2.1" }
  ```

  Bei `runtime: own` werden genau diese geladen.  Bei `runtime: shared` wird geprüft, ob der Wirt
  sie in **dieser** Version hat — fehlt sie oder weicht die Version ab, ist das ein Fehler vor dem
  ersten Schritt, kein stiller Fehltreffer mitten im Lauf.
- **F62-75** Die Version gehört in den Schlüssel, nicht nur in die Anforderung.  Sonst trifft ein
  Schritt, der gegen `2.0` gebaut wurde, für einen Lauf mit `2.1`.

**Was das von der Laufzeit verlangt** (aiml `spec/29_2` §16/§17): einen Symbolraum **je Kontext**.
Der heutige Weg über `DynamicLibrary::AddSymbol` ist flach, prozessweit und laut LLVM-Header
"permanent" — damit ist weder eine zweite Version noch ein Entladen möglich.  Nötig ist entweder
ein eigener Symbol-Resolver je Engine (bleibt bei MCJIT) oder ORCs **JITDylibs**, die genau dafür
gemacht sind.  Die Versionsfrage entscheidet diesen Punkt, nicht die Geschwindigkeit.

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
  registry reference as above.  `oci:<pfad>` is an **OCI Image Layout on disk** — the same
  directory shape a registry serves, only local: `oci-layout`, `index.json`, `blobs/sha256/…`.
  Das ist kein zweites Format und kein Sonderweg, sondern derselbe Vertrag ohne Netz; cosmos
  schreibt ihn heute schon (`cosmos/doc/spec/110` §110.25).  Es ist die Form, die eine
  Bau-Pipeline braucht, bevor es ein Registry gibt — und die, mit der ein Lauf sein Ergebnis
  dem **nächsten** Lauf als Basis hinlegt (`image:` liest dieselbe Adresse).
- **F62-123** Serialisiert wird dort, wo der Baum liegt (cosmos F96-27), und das gilt auch lokal:
  der Knoten schreibt das Layout, der Controller merkt sich nur die Adresse.  Liegt das Ziel auf
  einem anderen Rechner als der Baum, reisen die Blobs über den inhaltsadressierten Weg aus
  cosmos §96.2 — nicht über den Controller.

What this needs beyond the form: a root filesystem to start from, a deterministic layer
serialization, whiteouts for deletions and full file metadata.  All four are collected in cosmos
`doc/spec/96` §96.12 with their findings; none of them touches the stage/step form.

### 18.1 `image:` ohne Laufzeit — und der lokale Lauf

Eine Pipeline mit `image:` verlangt ein **Wurzel-Dateisystem**.  Der lokale JIT-Lauf (§12) hat
keines: er läuft auf dem Rechner, auf dem jemand `run_<name>` aufgerufen hat.  Das ist kein
Mangel — es ist der schnelle Weg, und er soll bleiben.  Aber es ist auch nicht dasselbe.

- **F62-124** `image:` ohne Bereitsteller ist ein **Fehler**, kein Hinweis und kein stiller
  Rückfall auf das Dateisystem des Entwicklers.  Begründung ist F62-65 in seiner schärfsten
  Form: ein Lauf, der gegen `/usr` des Laptops baut und trotzdem „gegen ubuntu:24.04" heißt,
  ist schlimmer als ein Lauf, der gar nicht startet.  Die Meldung nennt **drei** Dinge — das
  verlangte Abbild, dass hier keine Laufzeit da ist, und den Weg dorthin (ein Lauf über einen
  Wirt, der Container kann).
- **F62-124a (Nachtrag 25.10.2026): abgelehnt wird beim LAUF, nicht beim Übersetzen.**  Die
  Begründung von F62-124 ist ein **Lauf**, der behauptet, woanders gelaufen zu sein — nicht eine
  Datei, die ein Abbild nennt.  Bis hierher lehnte die Umschreibung schon das Übersetzen ab; damit
  konnte eine Pipeline, die für einen Cluster gedacht ist, `--check` **nie** bestehen, und ein
  Kontroller musste sie selbst lesen (§20.1 — genau die Doppelung, die weg soll).  Also: `image:`,
  `push:`, `agent:`, `steplayer:`, `base:` übersetzen und stehen als Daten in `pipeline`/`stage`
  (§20.4); **`pipe.run` lokal** gibt **-4** und lehnt mit derselben Meldung ab, die F62-124 verlangt — drei Dinge,
  samt Weg.  Die Zusage bleibt wörtlich erhalten, sie fällt nur an der Stelle, an der sie gilt.
- **F62-125** Ohne `image:` läuft eine Pipeline **gegen den Rechner**, und das ist die Vorgabe.
  Der lokale Lauf braucht dafür kein Container-Konzept (§12) und soll keines bekommen.
- **F62-132** Geprüft wird auf **beiden Ebenen** — am Kopf der Pipeline und an jeder Stufe.
  Ein `image:` am Kopf ist der häufigere Fall (es gilt für alle Stufen), und genau dort wäre
  ein stilles Weglassen am teuersten: der Autor liest „ubuntu-base", der Lauf nimmt den Wirt,
  und niemand sieht den Unterschied am Ergebnis.  Negativproben:
  `tests/pipeline_declared_neg/image_rejected.aiml` (Kopf) und `push_rejected.aiml`.

### 18.1a `resources:` — was die Stufe an Maschine braucht

```yaml
- pipeline:
    name: aiml_bauen
    image: ubuntu-base
    resources: { cpu: 4, mem: "8G", pids: 2048 }    # gilt fuer alle Stufen
    steps:
      - stage:
          name: bauen
          resources: { cpu: 8, mem: "12G" }          # ueberschreibt sie
```

- **F62-133** `resources:` nennt **Grenzen**, keine Wünsche: `cpu` in ganzen Kernen, `mem` mit
  Einheit (`512M`, `8G`), `pids` als Zahl.  Der Wirt setzt sie als cgroup-Grenzen des
  Containers (cosmos `doc/spec/110` §110.34).  Ohne Angabe gilt, was der Wirt vorgibt — eine
  Pipeline, die nichts sagt, bekommt die Vorgabe und nicht „unbegrenzt".
- **F62-134** `resources:` ist **kein Schlüsselmaterial** (F62-64, Fall 3), und das ist eine
  Aussage über *Ergebnisse*, nicht über Bequemlichkeit: mehr Kerne machen denselben Bau
  schneller, nicht anders.  Eine zu knappe **Speichergrenze** kann einen Schritt allerdings
  töten — aber dann **scheitert** er, und ein Fehlschlag wird nicht aufgezeichnet (F62-92).  Eine
  Grenze kann also einen Erfolg in einen Fehlschlag verwandeln, nie in einen *anderen Erfolg*;
  darum bleibt der Schlüssel sauber, und ein Cache-Treffer bleibt richtig.
- **F62-135** Die Grenze gehört in die **Pipeline**, nicht in den Wirt, sobald eine Stufe etwas
  Besonderes braucht.  `COSMOS_RT_LIMIT_MB` galt für jeden Container gleich — das heißt entweder
  jede Stufe bekommt, was die teuerste braucht (und ein Knoten trägt nur eine), oder die teuerste
  stirbt.  Beides ist falsch, und keines davon steht in der Pipeline, wo es jemand nachlesen
  könnte.

### 18.1b `shares:` — ein Netzlaufwerk im Container

Kerne und Speicher sind das eine; manche Stufe braucht aber **Platz, der den Lauf überlebt**:
ein Netzlaufwerk mit Quellen, ein Ablage-Verzeichnis für Artefakte, ein Paket-Cache, der nicht
bei jedem Lauf neu aus dem Netz gezogen wird.

```yaml
- pipeline:
    name: aiml_bauen
    image: ubuntu-base
    shares:
      - { name: quellen, to: /mnt/quellen }              # lesen (Vorgabe)
      - { name: artefakte, to: /mnt/out, mode: rw }      # schreiben
    steps:
      - stage:
          name: bauen
          shares:
            - { name: ccache, to: /root/.ccache, mode: rw }   # zusaetzlich, nur hier
          run: "ls /mnt/quellen && make -j8"
```

- **F62-136** `shares:` nennt einen **NAMEN**, nie einen Pfad.  Wo die Ablage liegt — eine
  NFS-Einhängung, eine SMB-Freigabe, ein Verzeichnis auf der Platte — weiß der **Wirt**, genau
  wie bei `image:` (F62-66/67; cosmos `doc/spec/110` §110.35).  Der Grund ist nicht Geschmack:
  eine Pipeline ist Nutzereingabe.  Dürfte sie `/srv/...` oder `/` hinschreiben, könnte jede
  eingereichte Pipeline jedes Verzeichnis des Wirts in ihren Container holen — und bei `mode: rw`
  auch beschreiben.  Ein Name, den der Betreiber vorher zugeordnet hat, kann das nicht.
- **F62-137** Die Vorgabe ist **`ro`**.  Schreiben muss dastehen.  Eine Freigabe, die ein Lauf
  versehentlich beschreibt, beschädigt die Läufe aller anderen; das ist der teuerste Fehler in
  dieser Ecke und darum der, der Absicht verlangt.
- **F62-138** `shares:` der Pipeline und der Stufe **addieren** sich; gleicher `to:` an beiden
  Stellen heißt, die Stufe gewinnt.  Dieselbe Regel wie bei `resources:` — eine Stufe darf etwas
  Besonderes brauchen, ohne dass die Pipeline für alle umgeschrieben wird.
- **F62-139** `shares:` **ist** Schlüsselmaterial (F62-64, Fall 1), aber nur mit `name` und `to`
  — nicht mit dem Inhalt.  Was ein Schritt an `/mnt/quellen` sieht, kann sich zwischen zwei
  Läufen ändern, ohne dass irgendjemand es merkt; der Schlüssel kann das nicht abbilden, und
  eine Aufzeichnung, die so tut, wäre eine Lüge über die Reproduzierbarkeit.  Die ehrliche Folge
  steht schon in §7: die Vorgabe ist `cache: opaque`.  Wer eine Stufe mit `shares:` trotzdem als
  `pure` erklärt, behauptet, dass der Inhalt der Freigabe zum Schlüssel gehört — und muss dafür
  selbst sorgen (etwa, indem die Freigabe unveränderlich ist).

### 18.4 `base:` — auf den Schichten eines anderen Laufs aufsetzen

`image:` sagt, **worin** ein Lauf arbeitet.  `base:` sagt, **worauf** er aufsetzt:

```yaml
- pipeline:
    name: 9rooms_bauen
    base: "job:aiml_bauen"               # der juengste ERFOLGREICHE Lauf dieses Jobs
    # base: "run:aiml-vom-Base-Image-5"  # oder: genau dieser Lauf
    steps:
      - stage: { name: bauen, run: "exodus build -c exodus-game.json" }
```

- **F62-140** `base:` nimmt den **Schichtstapel** eines früheren Laufs als Unterbau.  Gelesen
  wird aus ihm, **geschrieben immer in die eigene Schicht** — der andere Lauf bleibt unberührt,
  und zwei Läufe können denselben Unterbau gleichzeitig benutzen.  Die Form ist heute
  `run:<lauf>`; ein Name, den der Betreiber zuordnet, kann folgen, ändert aber nichts an der
  Bedeutung.
- **F62-141** `base:` gilt für den **ganzen Lauf**, nicht für eine Stufe.  Ein Stapel ist der
  Boden, auf dem alle stehen; eine Stufe, die auf einem anderen Boden stünde als ihre Vorgängerin,
  wäre kein Lauf mehr, sondern zwei.
- **F62-142** `base:` ist **kein Ersatz für `image:`**, sondern die andere Richtung.  Ein Abbild
  ist der Weg nach **draußen**: es trägt alles in sich, lässt sich wegkopieren und anderswo
  auslegen.  Ein Stapel ist der Weg **nebenan**: er kostet nichts, setzt aber voraus, dass beide
  Läufe auf demselben Wirt und demselben Grundabbild stehen (cosmos `doc/spec/110` F110-114).
  Wer das eine für das andere hält, baut ein Abbild, das nur zu Hause funktioniert.
- **F62-144** `base:` kennt zwei Formen.  `run:<lauf>` nennt **genau diesen** Lauf;
  `job:<kennung>` nimmt den **jüngsten erfolgreichen** Lauf dieses Jobs.  „Letzter Lauf"
  ohne „erfolgreich" gibt es bewusst **nicht**: ein fehlgeschlagener Lauf hat eine halbe
  Schicht hinterlassen, und darauf zu bauen hieße, auf einem Zustand zu bauen, den nie jemand
  gesehen hat.  Wer genau das will (eine kaputte Stufe untersuchen), nennt den Lauf beim Namen
  — dann steht die Absicht in der Pipeline und ist nicht die stille Vorgabe.
- **F62-145** `job:…` wird **bei der Anlage des Laufs** aufgelöst, nicht bei jeder Zuteilung.
  Danach steht `run:<lauf>` im Datensatz, und das Protokoll nennt beides
  (`'job:aiml-vom-Base-Image' → run:aiml-vom-Base-Image-5`).  Ein Unterbau, der sich mitten im
  Lauf ändert, wäre ein Lauf auf zwei verschiedenen Böden; und ein Lauf, dessen Grundlage man
  hinterher nicht mehr benennen kann, ist nicht nachvollziehbar.
- **F62-143** Was weitergegeben werden soll, gehört in die **Wurzel**, nicht in den
  Arbeitsplatz.  `/ws` ist der Arbeitsplatz des Laufs und wandert nicht mit; ein editierbarer
  Einbau dorthin (`pip install -e`) hinterlässt im Stapel einen Verweis ins Leere
  (`doc/spec/110` F110-115).  Fest einbauen, dann liegt es in der Schicht.

### 18.5 `agent:` und `steplayer:` — wer nimmt den Schritt, und entsteht dabei eine Schicht?

Zwei Fragen, die vorher in einem Wort steckten (`runs_on:`) oder gar nicht gestellt wurden:

```yaml
- pipeline:
    name: pruefen
    steps:
      - stage: { name: schnell, agent: x86_64, steplayer: none, run: "make -j8 test" }
      - stage: { name: sauber, steplayer: own, image: ubuntu-base, run: "./von_vorn.sh" }
```

- **F62-146** `agent:` sagt, **WER** den Schritt nimmt — ein **Etikett** (jeder Agent mit dieser
  Marke) oder eine **Agenten-Kennung** (genau dieser).  Der Name ist bewusst nicht `node:`: ein
  Container-Agent ist kein Knoten, er wohnt auf einem.  Was ausführt, ist in beiden Fällen ein
  Agent.  (Hieß bis 25.10.2026 `runs_on:`, davor `label:`; beide alten Namen werden abgewiesen
  und nennen den neuen.)
- **F62-147** `steplayer:` sagt, **OB** jede Stufe in ihre **eigene Schicht** schreibt: `own`
  (eigene Wurzel aus einem Abbild, Schicht je Stufe, eigener Arbeitsbereich — cosmos
  `doc/spec/110` F110-93) oder `none` (kein Schichtenbau; der Schritt arbeitet auf dem
  Dateisystem des Agenten, eingesperrt mit bwrap).  Ohne Angabe entscheidet `image:`: steht eines
  da, ist `own` gemeint, sonst `none`.  Die Angabe ist nie nötig und immer erlaubt — und
  `steplayer: none` **zusammen mit** `image:` ist ein Widerspruch, den die Einreichung abweist,
  statt eines von beiden gewinnen zu lassen.
- **F62-148** `steplayer:` ist **Schlüsselmaterial** (F62-64, Fall 1).  Derselbe Befehl
  auf dem Dateisystem eines Agenten und in einer frischen Wurzel sieht Verschiedenes — ein
  gemeinsamer Cache-Eintrag wäre ein falscher Treffer.  Sie gehört damit in den Schritt-Schlüssel
  wie `image:` selbst.
- **F62-149** Der Zuteiler **wählt nicht**.  Er weicht nicht auf einen Knoten aus, weil kein
  Container frei ist, und startet keinen Container, weil die Knoten beschäftigt sind.  Wer
  Ausweichen will, schreibt zwei Stufen und sagt, welche wann gilt — dann steht die Absicht in
  der Pipeline und nicht in der Laune des Servers (cosmos `doc/spec/110` F110-124).

### 18.2 `requires:` — was die Umgebung mitbringen muss

Dieselbe Pipeline läuft lokal und im Container.  Lokal entscheidet der Rechner, ob `git`, `gcc`
oder `llvm-config-18` da sind; im Container entscheidet das Abbild.  In beiden Fällen gilt:
**fehlt etwas, soll es in der ersten Sekunde auffallen und nicht in der fünften Minute** —
mitten im Übersetzen, mit einer Meldung über eine fehlende Header-Datei, die niemanden auf das
fehlende Paket bringt.

```yaml
- pipeline:
    name: aiml_bauen
    image: "oci:deploy/oci/ubuntu-base"
    requires: [git, "gcc>=13", llvm-config-18, "lib:yaml-cpp"]
```

- **F62-126** `requires:` ist eine Liste von **Namen**: ein ausführbares Programm (`git`), mit
  optionaler Mindestversion (`gcc>=13`), oder eine Bibliothek über `lib:<pkg-config-Name>`.
  Geprüft wird **vor dem ersten Schritt** der Stufe, **einmal**, und die Meldung nennt **alles**
  Fehlende auf einmal — nicht das erste, über das man stolpert.  Eine Prüfung, die nach dem
  ersten Fehler aufhört, erzeugt drei Läufe statt einem.
- **F62-127** `requires:` ist **kein Schlüsselmaterial** (F62-64, Fall 3).  Dass ein Werkzeug
  *da ist*, ändert kein Ergebnis — dass es eine *bestimmte Version* ist, sehr wohl, und genau
  dafür gibt es `host:` (§10).  Die Trennung ist die Regel aus F62-49, nur eine Ebene tiefer:
  Vorbedingung prüfen ≠ Eigenschaft in den Schlüssel nehmen.
- **F62-128** `requires:` ersetzt `image:` **nicht** und erklärt es auch nicht weg.  Es ist die
  Zusage, die eine Pipeline über ihre Umgebung macht; `image:` ist die Umgebung.  Eine Pipeline
  darf beides nennen — dann prüft der Container gegen die eigene Zusage, und ein Abbild, das
  seine eigene Pipeline nicht tragen kann, fällt beim Bauen des Abbilds auf, nicht beim Bauen
  des Produkts.

### 18.3 Kausalität: ein Abbild, das eine Stufe erst herstellt

`push:` schreibt ein Abbild, `image:` liest eines — und beides darf in **derselben** Pipeline
stehen.  Genau dann entsteht eine Reihenfolge, die niemand hingeschrieben hat:

```yaml
    steps:
      - stage: { name: basis,  push: [{ from: basis, to: "oci:deploy/oci/builder" }], ... }
      - stage: { name: bauen,  image: "oci:deploy/oci/builder", ... }
```

- **F62-129** Nennt eine Stufe als `image:` ein Ziel, das eine **andere Stufe derselben
  Pipeline** schreibt, ist das eine **Kante im Graphen** — dieselbe Wirkung wie ein `needs:`,
  nur hergeleitet statt hingeschrieben.  Ohne diese Herleitung liefe die zweite Stufe gegen das
  Abbild des **vorigen Laufs**: grün, falsch, und beim nächsten Mal wieder anders.  Der Wirt
  leitet die Kante aus dem Vergleich der Adressen ab und meldet einen Zyklus als Fehler.
- **F62-130** In den Schlüssel geht die **Digest des geschriebenen Layouts**, nicht sein Pfad
  (F62-68 für Tags, hier für lokale Ziele).  Ein Pfad ist ein Name und bewegt sich; die Digest
  ist der Inhalt.  Erst damit ist ein Lauf, der ein Abbild baut und darin weiterbaut,
  reproduzierbar — und erst damit darf die zweite Stufe aus dem Cache kommen.
- **F62-131** Ein `image:`, das auf ein Ziel zeigt, das **niemand** in dieser Pipeline schreibt
  und das es auch nicht gibt, ist ein Fehler **vor** dem Start — nicht beim Zuteilen der Stufe.
  Der Lauf, der zehn Minuten arbeitet und dann an der letzten Stufe scheitert, weil ihr Abbild
  nie existierte, ist die teuerste Art, es zu merken.

## 19. Umsetzungsstand (2026-10-11)

Diese Spec beschreibt mehr, als heute laeuft.  Damit sie keine Zusagen macht, die der Code nicht
haelt, steht hier, was **gemessen** funktioniert und was offen ist.  Jede Zeile nennt den Test,
der sie haelt — eine Zeile ohne Test gilt als nicht umgesetzt, egal wie ueberzeugend der Code
aussieht.

### Umgesetzt und getestet

| Thema | Wo | Test |
|---|---|---|
| Matrix, Lanes, `exclude` als Teilbindung (§4) | `pipeline_runtime.cpp` | `tests/test_pipeline_matrix.aiml` |
| `pipeline`/`stage`/`step` als Werte (§13.5) | `builtin_registry.cpp` | `tests/test_pipeline_types.aiml` |
| Beobachter je (Schritt, Lane) (§13.2) | `AimlStepObserver` | `tests/test_pipeline_observer.aiml` |
| `args`-Teilmenge = gröberer Schluessel (§5.2) | `step_key` | `tests/test_pipeline_args_subset.aiml` |
| Wiederverwendungsklassen + Sonde (§7, §9) | `step_key`, `set_resolve` | `tests/test_pipeline_resolve.aiml` |
| `on_fail`, `timeout`, `retry`, Status (§5.6, §5.7) | `run_lane` | `tests/test_pipeline_failure.aiml` |
| Anhalten/Fortsetzen (§5.9) | `request_pause`, `release` | `tests/test_pipeline_pause.aiml` |
| Parallele Lanes (§6) | `run_lane` + Threads | `tests/test_pipeline_parallel.aiml` |
| Gleichzeitiger Prozessstart (F62-14) | — (Messung) | `tests/test_concurrent_spawn_hazard.aiml` |
| Lane-Sicht im Rumpf, Arbeitsbereich (§4.3, §13.5e) | `step.arg`/`step.workspace` | `tests/test_pipeline_lane_access.aiml` |
| Reinheitspruefung (§10.2) | `purity.h` | `tests/pipeline_purity/run.sh` |
| Deklarierte Form (§5) | `pipeline_expand.cpp` | `tests/test_pipeline_declared.aiml`, `tests/pipeline_declared_neg/run.sh` |
| Rumpf ueber die Leitung (§5.8, Teil) | `sys.bundle`/`sys.exec` | `tests/bundle_socket/run.sh` |
| `requires:` lokal geprüft (§18.2) | `pipeline_expand` + `sys.require_tools` | `tests/test_pipeline_requires.aiml` |
| `sys.require_tools` (spec/23 §5.6) | `process_runtime.cpp` | `tests/test_sys_require_tools.aiml` |
| `image:`/`push:` lokal abgelehnt, mit Weg (§18.1 F62-124/132) | **beim LAUF** (F62-124a), nicht beim Übersetzen | `tests/test_pipeline_local_refuses.aiml` |
| `image:` als Zuteilungskriterium (§18, F62-66/67) | cosmos `step_scheduler`, `rt_launcher` | cosmos `test/test_step_dispatch.aiml` |
| `push: oci:<pfad>` + hergeleitete Reihenfolge (F62-122/129) | cosmos `push_worker`, `cluster_bootstrap` | cosmos `test/test_step_dispatch.aiml` + `doc/spec/110` §110.30 (Messung) |

### Offen, mit Grund

- **Datenfluss fuer eigene TYPEN (Rest von F62-33/F62-34).**  Zeichenketten und Zahlen fliessen
  (§13.5h, F62-121): der Ergebniskanal laeuft ueber die Laufzeit statt ueber den Rueckgabewert der
  JIT-Funktion, weil letzterer nicht abholbar ist (gemessen,
  `tests/test_bundle_exec_semantics.aiml`).  Ein **Modell** muss der Autor heute selbst mit
  `sys.serialize`/`sys.deserialize` durch denselben Kanal schicken; dass der Compiler das nicht
  uebernimmt, ist der verbleibende Teil.  `list<T>` ueber die Lanes (F62-8, fan-in) fehlt damit
  ebenfalls noch.
- **`runs_on` und echter Transport auf einen Knoten (§5.8).**  Das Bundle reist nachgewiesen
  ueber eine Verbindung (`tests/bundle_socket`), aber die Pipeline selbst waehlt keinen Knoten
  aus; `runs_on` wird abgelehnt.
- **`outputs:` und der Ausgabebaum (§16 Punkt 5).**  Der Arbeitsbereich existiert, wird aber
  nicht gehasht und nicht uebertragen — der Cache haelt heute nur den Exit-Code, nicht den Baum.
  Das ist die groesste Luecke zwischen dieser Spec und cosmos `doc/spec/96`.
- **Zeitgrenze erzwingen (F62-40).**  `timeout` wird **festgestellt**, nicht erzwungen: der Rumpf
  laeuft aus, danach gilt der Schritt als gescheitert, und das Log sagt es ausdruecklich.  Kinder
  zu toeten braucht den Griff auf die Kindprozesse des Rumpfs, den der isolierte Kontext heute
  nicht herausgibt.
- ~~Gleichzeitiges Uebersetzen (F62-99)~~ — **gemessen und entschieden** (F62-99): Threads
  bringen beim isolierten Uebersetzen nichts (8 Threads ~1030 ms gegen einthreadig ~960 ms fuer
  dieselben 24 Uebersetzungen), das Schloss bleibt.
- **Container-URL und Push (§17), 3D-Darstellung.**  Zielbild, nichts davon umgesetzt.
- **Die Bedeutung der sechs Farben (§16 Punkt 6).**  Bewusst offen; die Praezedenzordnung steht
  als vorlaeufige Tabelle an einer Stelle (F62-93).

## 20. Die übersetzte Pipeline LESEN — ein Kontroller ist ein Leser, kein Parser

**Vorgabe des Nutzers (25.10.2026):** *„Die Idee war, dass der Pipeline-Code durch den Compiler
geschoben wird.  Cosmos selber löst also keine aiml-Knoten auf."*

### 20.1 Warum

Heute lesen **zwei** Programme dieselbe Sprache: der Übersetzer (`pipeline_expand.cpp`, §5) und
der Kontroller (cosmos `sources/server/pipeline_submit.aiml`, 180 `yaml.*`-Aufrufe).  Beide
kennen `params`, `exclude`, `stages`, `steps`, beide bilden Lanes, beide prüfen Formen — und sie
kennen **verschiedene Teile**: der Übersetzer die Reinheit und den Schlüssel, der Kontroller
`agent:`, `steplayer:`, `image:`, `base:`, `shares:`, die Grenzen.  Zwei Leser derselben Sprache
laufen auseinander, und zwar leise: ein Schlüssel, den der eine kennt und der andere nicht, ist
entweder eine stille Wirkungslosigkeit oder eine Ablehnung ohne Grund.

Die Richtung ist deshalb nicht „der Kontroller lernt die restlichen Schlüssel", sondern **der
Kontroller liest überhaupt nicht mehr**: er gibt den Quelltext in einen Kontext (spec/29_2 §14a/§14b),
lässt ihn übersetzen und **liest die entstandene Pipeline als Wert**.

```yaml
# Der ganze Weg, ohne eine Zeile YAML im Kontroller:
- decl: [kontext, ctx_handle, { call: [ctx.new] }]
- decl: [rcx, i32, { call: [kontext.exec, { call: [sys.code_from, eingereicht] }] }]
- if: { condition: { lt: [rcx, 0] }, then: [ ... kontext.errors an den Einreicher ... ] }
- decl: [namen, list<string>, { call: [kontext.names, "fn() -> pipeline"] }]
- decl: [pipe, pipeline, { call: [kontext.call, { at: [namen, 0] }, "fn() -> pipeline"] }]
- decl: [lanes, i32, { call: [pipe.lane_count] }]
- decl: [stufen, i32, { call: [pipe.stage_count] }]
```

### 20.2 Wer prüft was

| | prüft | Beispiel |
|---|---|---|
| **Übersetzer** | die **Form**: Schlüssel, Typen, Achsen, Verweise, Reinheit, Widersprüche | `steplayer: own` ohne `image:`; `needs: [gibtesnicht]`; unbekannter Schlüssel; `exclude` auf eine Achse, die es nicht gibt |
| **Kontroller** | die **Lage**: was es hier gibt und wer darf | kennt dieser Cluster das Abbild `aiml-build`?  ist das Etikett `x86_64` an einem Agenten?  darf dieser Nutzer so viele Lanes? |

- **F62-150 Formfehler gehören dem Übersetzer, Lagefehler dem Kontroller.**  Die Trennlinie ist,
  ob die Antwort im Quelltext steht: „`steplayer: own` ohne `image:`" ist im Quelltext
  entscheidbar und daher ein **Übersetzungsfehler** — überall gleich, auch beim lokalen Lauf, und
  nur **einmal** geschrieben.  „Dieses Abbild gibt es hier nicht" ist von der Maschine abhängig
  und daher Sache des Kontrollers; ein Übersetzer, der sie behauptet, lügt auf jedem zweiten
  Cluster.  Die alten Namen (`runs_on:`, `label:`, `runs_in:`) sind damit ebenfalls Formfehler:
  **der Übersetzer** nennt den neuen Namen, nicht jeder Wirt für sich.

### 20.3 Was der Übersetzer vor dem Lesen schon getan hat

- **Lanes gebildet**, `exclude` abgezogen (§4) — der Leser bekommt die Zahl und je Lane ihren
  **Bindungstext** in der kanonischen Form `achse=wert,achse=wert` (sortiert, §8).  Genau die
  Form, die auch der Beobachter (§13.1) und der Arbeitsbereichspfad (§4.3) tragen.
- **Vorrang aufgelöst** (§18, F62-133/F62-138): `image:`, `timeout:`, `requires:`, `agent:`,
  `steplayer:` und die Grenzen stehen an der Stufe mit dem Wert, der **gilt** — Stufe schlägt
  Pipeline; `shares:` addieren sich.
- **Vorgaben hergeleitet** (F110-125): `steplayer` ist `own`, wenn ein `image:` gilt, sonst `none`.

- **F62-151 Vorrang wird EINMAL aufgelöst, im Übersetzer.**  Sonst rechnet jeder Wirt die
  Regeln nach, und zwei Wirte sind irgendwann uneins darüber, was `timeout:` an der Pipeline für
  eine Stufe mit eigenem `timeout:` bedeutet.  Ein Leser, der nachrechnen muss, ist ein zweiter
  Parser mit anderem Namen.
- **F62-152 Der Bindungstext ist EIN Format.**  Lane-Text, Beobachter-`bindings`,
  Arbeitsbereichsname und der Datensatz des Kontrollers (cosmos F96-34) tragen dieselbe
  Zeichenkette.  Zwei Formate für denselben Begriff heissen: irgendwo wird Anzeigetext geparst.

### 20.4 Die Leser

Alles **nur lesend**; ein gelesener Wert ändert die Pipeline nicht.

| Pipeline | gibt | Bedeutung |
|---|---|---|
| `pipe.name` | string | Name aus der Deklaration |
| `pipe.lane_count` | i32 | Lanes nach `exclude` (§4.3) |
| `pipe.lane_bindings(i)` | string | `arch=x86_64,mode=debug` (F62-152) |
| `pipe.stage_count` | i32 | Stufen je Lane |
| `pipe.stage_at(i)` | stage | die i-te Stufe |
| `pipe.axis_count` / `pipe.axis_name(i)` | i32 / string | die Achsen in Deklarationsreihenfolge |
| `pipe.axis_value_count(i)` / `pipe.axis_value(i, j)` | i32 / string | ihre Domäne (§4.1) — Anzahl und Wert, **keine Liste**: ein `list<string>` aus der Laufzeit zurückzugeben kostet eine Füll-Schnittstelle je Leser, und zwei Zahlenleser sagen dasselbe |
| `pipe.push_count` / `pipe.push_from(i)` / `pipe.push_to(i)` / `pipe.push_tag(i)` | i32 / string | `push:` (§18, F62-69/71) |

| Stufe | gibt | Bedeutung |
|---|---|---|
| `stg.name` | string | |
| `stg.agent` | string | **WER** — Etikett oder Agenten-Kennung (F110-127) |
| `stg.steplayer` | string | **OB** — `own` \| `none` (§18.5, F110-124) |
| `stg.image` | string | **WORIN** — Abbild oder `oci:<pfad>` (F62-66) |
| `stg.base` | string | **WORAUF** — `run:<lauf>` \| `job:<kennung>` |
| `stg.requires` | string | `git,gcc>=13,lib:yaml-cpp` (§18.2, F62-126) |
| `stg.shares` | string | `name:/ziel:modus,…` (§18.1b, F62-136) |
| `stg.cpu_cores` / `stg.mem_mb` / `stg.pids_max` | i32 | Grenzen, 0 = Vorgabe des Wirts (§18.1a) |
| `stg.timeout_ms` | i32 | 0 = Vorgabe des Ausführenden (F62-40) |
| `stg.command` | string | `run:` — der Befehl der Scheibe 1 (cosmos F103-1) |
| `stg.step_count` / `stg.step_at(i)` | i32 / step | die Schritte der Stufe (§5) |

| Schritt | gibt | Bedeutung |
|---|---|---|
| `stp.name` | string | |
| `stp.needs_count` / `stp.need_at(i)` | i32 / string | `needs:` (§5.1) |
| `stp.cache` | string | Wiederverwendungsklasse (§7) |
| `stp.per` | string | `lane` \| `once` (§5.5) |
| `stp.command` | string | `run:` am Schritt, sonst leer |

- **F62-153 Ein Leser gibt Werte, keine Zeiger in den Übersetzer.**  `stage` und `step` sind die
  Werte aus §13.5 — dieselben, die man zusammensetzen kann.  Es gibt keinen Weg vom gelesenen
  Wert zum AST, zum LLVM-Typ oder zur Symboltabelle (spec/29_2 F29_2-…m): sonst hängt der
  Kontroller am Innenbau des Übersetzers.
- **F62-154 Fehlt etwas, ist es leer bzw. 0 — nicht „unbekannt".**  Ein Leser wirft nicht, weil
  ein Schlüssel nicht geschrieben wurde; `0` heisst bei den Grenzen ausdrücklich „die Vorgabe des
  Wirts" und nicht „unbegrenzt" (§18.1a).  Ein Index ausserhalb des Bereichs ist dagegen ein
  Programmfehler des Lesers und **wirft** (spec/22).
- **F62-155 Die Stufen sind lane-UNabhängig, die Bindungen lane-abhängig.**  Darum `stage_at(i)`
  ohne Lane und `lane_bindings(i)` ohne Stufe: das Kreuzprodukt baut der Leser, weil nur er weiss,
  ob er eine Zeile je (Lane, Stufe) braucht (cosmos) oder je Stufe (eine Übersicht).  Eine
  Leserfunktion, die beides mischt, müsste `(lane, stufe)` nehmen und für jede Abfrage dieselbe
  Stufe neu beschreiben.

### 20.4a Was beim Umbau noch aufgefallen ist (25.10.2026)

Drei Stellen, an denen Spec und Umsetzung auseinander waren — gefunden, weil der Kontroller
seitdem **denselben** Leser benutzt wie der Übersetzer.  Jede ist hier festgehalten, damit sie
nicht ein zweites Mal auseinanderlaufen:

- **F62-156 `steps:` auf beiden Ebenen, wie F62-57 es sagt.**  Die Umschreibung las `stages:`,
  die Spec schrieb `steps:` — und cosmos' 15 Pipelines waren die ganze Zeit spec-konform, der
  Übersetzer der Ausreißer.  Jetzt gilt `steps:`; `stages:` wird abgewiesen und nennt den
  richtigen Schlüssel.  Dasselbe für `params:`: die Umschreibung nahm die Abbildung
  `arch: [...]`, §4.1 verlangt die **typisierte Liste** `[name, typ, [werte]]`, und cosmos'
  Tests benutzten die Liste.  Der Typ wirkt jetzt: im Rumpf ist eine `i32`-Achse ein `i32`
  (F62-1), gewandelt an **einer** Stelle im erzeugten Vorspann.
- **F62-157 Der Name ist Text, die Funktion braucht einen Bezeichner.**  `name: 9rooms_vom_stapel`
  fängt mit einer Ziffer an und war damit gar nicht übersetzbar — obwohl der Name völlig in
  Ordnung ist.  Ist der Name ein Bezeichner, heißt die Funktion genau so (F62-108 bleibt, man
  ruft sie lokal); sonst entsteht ein abgeleiteter Funktionsname, und der **Wert** trägt weiter
  den echten Namen (`pipe.name`).  Ein Wirt findet die Funktion über `cx.names`
  (spec/29_2 F29_2-…i) und braucht den Namen nicht zu raten.
- **F62-158 Ein Übersetzer MELDET, er fällt nicht um.**  `cache: haeufig` ließ den erzeugten
  Aufruf `set_cache(CacheClass.haeufig)` entstehen; die Enum-Auflösung gab `nullptr`, und die
  Builtin-Methode rechnete damit weiter — Absturz im Übersetzer, bei genau der Eingabe, die er
  gerade bemängelte.  Zwei Lehren, beide umgesetzt: `cache:` wird in der Umschreibung geprüft
  (§7), und ein Argument, das sich nicht übersetzen ließ, hält die Builtin-Methode an.  Bei
  einer **eingereichten** Pipeline ist das kein Schönheitsfehler: ein Absturz des Kontrollers
  ist eine Einreichung, die den Dienst beendet.

### 20.5 Was damit NICHT gemeint ist

- **Kein Lauf.**  Lesen führt nichts aus; `pipe.run` bleibt der lokale Lauf (§12).  Ein
  Kontroller liest und teilt **selbst** zu — die verteilte Ausführung ist seine Aufgabe, nicht die
  der Sprache (§14, F62-49).
- **Keine Freigabe.**  Der Kontext ist keine Sicherheitsgrenze (spec/29_2 F29_2-…e).  Wer fremden
  Quelltext übersetzt, übersetzt ihn **im eigenen Prozess**; die Grenze für fremden Code bleibt
  ein eigener Prozess (spec/65 F65-15).  Für cosmos heisst das: die Einreichung wird geprüft, der
  **Rumpf** läuft beim Agenten, nicht im Controller.
- **Kein Schutz vor feindlichem Quelltext.**  Werte, die als Literal in erzeugten Quelltext
  gehen, werden **maskiert** (`"` → `\"`, Zeilenumbruch → `\n`) — ohne das endete das Literal
  mitten im Wert und der Rest wäre aiml, also eingeschleuster Code.  Maskiert und nicht
  abgewiesen, weil ein `run:` legitim Anführungszeichen trägt.  Das macht die Umschreibung
  **robust**, nicht sicher: für feindliche Einreichungen bleibt die Grenze ein eigener Prozess
  (spec/65 F65-15), und ein Kontroller lässt eingereichten Quelltext nicht laufen
  (`cx.load`, spec/29_2 F29_2-…n).
- **Kein zweites Format.**  Es gibt keinen „Controller-Dialekt" der Pipeline-Sprache.  Was der
  Übersetzer nicht kennt, ist ein Fehler — auch wenn ein Wirt damit umgehen könnte.
