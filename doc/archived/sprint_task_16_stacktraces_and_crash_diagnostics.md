# sprint_task_16

Title: native crash stacktraces and AIML stacktraces as a shared diagnostic architecture

## Status

This document replaces:

- `architecture_sprint_plan.md`
- `sprint_task_15_source_locations_and_diagnostics.md`

Both documents were too coarse or too narrowly cut for the current state:

- the architecture paper was too broad and not diagnostic-centred
- the source location task was too narrow and did not consider runtime/crash paths

The goal of this document is a single actionable plan for:

- native stacktraces on `stderr`
- AIML stacktraces on `stderr`
- central source locations and compiler diagnostics
- the compiler/runtime/JIT/AOT interfaces needed for that

## Target picture

When AIML crashes or ends with a severe compiler/runtime error, the output on `stderr` should be considerably better.

Target picture 1: native crash

```text
=== AIML NATIVE CRASH (SIGSEGV) ===
Fault address: 0x...
Thread: main
Native stacktrace:
  0: aiml(+0x...)
  1: AIMLCompiler::jitRun()
  2: ...
```

Target picture 2: AIML runtime crash during code execution

```text
=== AIML NATIVE CRASH (SIGSEGV) ===
...
AIML stacktrace:
  0: Player.update at game/player.aiml:42:7
  1: Engine.tick at sys/engine.aiml:118:3
  2: main at main.aiml:7:1
```

Target picture 3: compiler/semantic error without a process crash

```text
frontend/session.aiml:413:15 Undefined property 'hp_maxx' on model 'Player'
```

## Why this is an architecture topic

The topic is not just a signal handler or just compiler polish.

It reaches into several levels:

- process start and fatal signal handling
- central logging/diagnostic interfaces
- AST source locations
- CompilerContext and error propagation
- IR emission for AIML call frames
- runtime storage for thread-local AIML frames
- JIT and AOT parity

If these parts are solved separately, point solutions arise again:

- mirror has its own crash handler
- compiler errors only know C++ files
- JIT has no AIML frames
- AOT and JIT behave differently

This is exactly what this sprint plan is meant to avoid.

## Current state

### Already present

- In `src/sys/mirror_runtime.cpp` there is already a local POSIX crash handler with `sigaction`, `backtrace()` and `backtrace_symbols_fd()`.
- AST nodes already carry `YAML::Node sourceNode` and partly `sourceLine`.
- For `log.*` there is already source context injection.
- Top-level decls are partly linked with YAML nodes during parse/compile.

### Not yet present

- a global, official crash handler for the whole process
- `std::set_terminate(...)` handling for uncaught C++ exceptions
- a normalised `SourceLoc` abstraction with file/line/column
- a central diagnostic API in the CompilerContext
- a thread-local AIML frame stack
- automatic push/pop instrumentation on AIML function calls
- a formatted AIML stacktrace on `stderr`
- clear JIT/AOT parity for diagnostics

## Affected C++ places

Process / entry:

- `main.cpp`
- `logger.h`
- optionally a new file block such as `src/core/crash_diagnostics.{h,cpp}`

Compiler / source locations / diagnostics:

- `compiler.cpp`
- `compiler.h`
- `src/core/ast_nodes.h`
- `src/core/ast_builder.cpp`
- `src/core/context.h`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_call.cpp`

Runtime / call stack / crash dump:

- `exec_impl.cpp`
- `src/sys/mirror_runtime.cpp`
- possibly further runtime entry points for host callbacks / threads

Tests / repros:

- `tests/`
- new negative crash/diagnostic repros

## Guard rails

### 1. Native crash output must stay signal-safe

Only very defensive code may run in the fatal signal handler.

Allowed:

- `write(2, ...)`
- `backtrace()` / `backtrace_symbols_fd()` as the pragmatic POSIX way
- simple static buffers
- re-raising the signal after resetting to default

Not in the signal handler:

- `std::string`
- `new`
- iostreams
- complex locks
- YAML / LLVM / other large libraries

### 2. AIML frames must be thread-local and robust

The AIML frame stack must not hang on a global singleton.

Requirement:

- `thread_local` storage
- a simple POD-like frame entry
- push/pop via RAII or clearly symmetric runtime calls

### 3. JIT and AOT must see the same semantics

It must not happen that:

- AOT has native stacktraces, but JIT has no AIML frames
- JIT has better AIML frames than AOT
- compiler diagnostics deliver more source info in one mode than in the other

### 4. Diagnostics must become central

No further growth of raw `LOG_ERROR(...)` paths without AIML context.

Instead:

- `ctx.error(node, msg)`
- `ctx.error(loc, msg)`
- `ctx.runtimeError(loc, msg)`
- optionally `ctx.note(...)`

## Architecture decision

There are two different but combinable stacktraces:

### A. Native stacktrace

Source:

- C++ frames
- LLVM/JIT/AOT runtime
- process signal / `terminate`

Benefit:

- finding native crashes
- narrowing down bad pointers / LLVM / runtime errors
- symbolised backtraces for C++ debugging

### B. AIML stacktrace

Source:

- explicitly maintained AIML call frames
- based on compiler metadata and runtime push/pop

Benefit:

- the user sees the AIML function chain instead of C++ internals
- diagnostics for JIT runtime problems become practically usable
- compiler/runtime errors can be related back to the AIML file/line

Both should exist side by side.

## Sprint structure

### Phase 0: foundation and defining the format

Goal:

- define the official diagnostic format
- no more parallel ad-hoc formats

Work:

- define the output format for native crash headers
- define the output format for AIML frames
- define the `SourceLoc` format: `file`, `line`, `column`, optionally `function`, `kind`
- decide on relative paths instead of absolute paths on `stderr`

Deliverables:

- a small diagnostic spec implemented in the code or in this document
- the required target strings for tests fixed

### Phase 1: centralise the global native crash handler

Goal:

- move the existing local crash logic from `mirror_runtime.cpp` into an official global path

Work:

- a new central component, e.g. `src/core/crash_diagnostics.cpp`
- an install function early in `main.cpp`
- signals at least:
  - `SIGSEGV`
  - `SIGABRT`
  - `SIGBUS`
  - optionally `SIGILL`
  - optionally `SIGFPE`
- `std::set_terminate(...)` for uncaught C++ exceptions
- a header on `stderr` with the signal name / thread context / a hint about symbolisation
- remove the existing mirror-specific installation or redirect it to the central API

Architecture decision:

- a single installer in the process
- no more subsystem-specific constructor hooks for crash handling

Acceptance criteria:

- a deliberate `SIGSEGV` prints a native stacktrace on `stderr`
- an uncaught C++ exception prints a `terminate` hint on `stderr`
- mirror no longer uses a separate special path

### Phase 2: introduce `SourceLoc` as the official compiler abstraction

Goal:

- AST/compiler/runtime paths communicate via a normal source location structure instead of implicitly via `YAML::Node`

Work:

- introduce a `SourceLoc` struct
- at least:
  - `file`
  - `line`
  - `column`
  - optionally `function_name`
  - optionally `context_kind`
- `Node` in `ast_nodes.h` extended or accompanied by `SourceLoc`
- helpers for converting `YAML::Node` / `YAML::Mark`
- normalise relative file paths to the project root

Important:

- `YAML::Node sourceNode` can stay for now
- `SourceLoc` becomes the official surface for diagnostics

Acceptance criteria:

- new AST nodes can carry `SourceLoc`
- `sourceLine` as a special case is no longer the main API

### Phase 3: complete the builder and AST propagation

Goal:

- relevant expressions, statements and top-level declarations reliably carry `SourceLoc`

Work:

- switch `ast_builder.cpp` to systematic loc propagation
- secure call, decl, set, if, while, return, throw, try and top-level nodes
- define fallback rules for helper nodes generated from YAML

Risk:

- today some nodes are only annotated selectively; that later leads to half-empty AIML frames

Acceptance criteria:

- the core nodes of the language have locations
- `log.*` stays compatible and ideally uses the same loc source

### Phase 4: centralise compiler diagnostics

Goal:

- no longer report semantic errors raw via scattered `LOG_ERROR(...)` calls

Work:

- central helpers in `CompilerContext`
- error format: `aiml_file:line:column message`
- switch the most frequent errors first:
  - unknown type
  - undefined variable
  - undefined property
  - wrong argument count
  - wrong argument type
  - invalid member access
- improve parser/import errors with the imported file
- enrich IR/verifier errors with the last known AIML context

Architecture decision:

- diagnostic output comes from one place
- logging is only transport, not the place where the format is decided

Acceptance criteria:

- typical semantic errors show the AIML file/line/column
- late compiler errors show at least the last responsible AIML context

### Phase 5: introduce the AIML frame stack in the runtime

Goal:

- keep own AIML call frames at runtime, independently of the native stack

Work:

- runtime struct `AimlFrame`
- a `thread_local` stack or a simple thread-local frame vector
- fields at least:
  - AIML function name
  - `SourceLoc`
  - optionally namespace / file / call kind
- runtime API:
  - `aiml_push_frame(...)`
  - `aiml_pop_frame()`
  - `aiml_dump_stacktrace_fd(int fd)`

Important design questions:

- push/pop as real runtime functions or inline helpers
- a max frame limit against recursion/corruption cases
- behaviour on stack imbalance in exceptions / early returns

Acceptance criteria:

- the runtime can dump the current AIML frame stack independently of the native stack
- multithreading does not smear frames between threads

### Phase 6: compiler instrumentation for AIML frames

Goal:

- JIT- and AOT-generated code maintains the AIML frames automatically

Work:

- `push_frame` on entry into AIML functions
- `pop_frame` on all return paths
- secure the symmetry on throws / exceptional exits
- if possible a RAII-like pattern at IR level or centralised epilogue paths
- check builtins / host callbacks:
  - whether they need AIML frames themselves
  - or only leave the calling AIML frame visible

Affected places:

- `ast_compiler.cpp`
- `ast_compiler_call.cpp`
- `ast_compiler_stmt.cpp`
- possibly the entry point generator

Risks:

- several return blocks
- exception paths
- tail-call-like optimisations
- JIT/AOT differences with wrapper functions

Acceptance criteria:

- in a normal run the frame stack consistently stays empty after the function ends
- on a crash in the middle of AIML code the last AIML chain is visible

### Phase 7: connect the native crash handler and AIML frames

Goal:

- a fatal signal prints both perspectives

Work:

- the signal handler first prints the native header and the native stacktrace
- afterwards, if robustly possible, output the AIML frames via a signal-safe dump path
- also dump the native + AIML stack for `std::terminate(...)`

Important restriction:

- Not everything in the AIML dump may be signal-unsafe.
- If `aiml_dump_stacktrace_fd(...)` only works with raw C strings/POD data, that is realistic.
- If too many `std::string` dependencies are needed after all, a prepared, signal-poor snapshot approach must be built for fatal signals.

Pragmatic decision:

- rather a reduced, robust AIML frame dump than a "pretty" but unreliable handler

Acceptance criteria:

- on a deliberate crash during an AIML run, the native plus AIML stacktrace appears on `stderr`

### Phase 8: symbolisation and build integration

Goal:

- native backtraces are readable enough to be practically useful

Work:

- standardise debug builds: `-g`
- for AOT, if possible, `-rdynamic` or an equivalent symbol export strategy
- document what remains visible in the release build
- optionally an environment flag for extended crash output

Not the goal of this phase:

- fully automatic external symboliser integration such as `addr2line` in the signal handler

Acceptance criteria:

- the debug build delivers usable C++ symbol names
- behaviour in release is documented

## Test plan

### Native crash tests

- a deliberate `SIGSEGV` in a small repro
- an `abort()` repro
- an uncaught `throw` exception

### Compiler diagnostic tests

- unknown type
- undefined property
- wrong argument type
- import failure
- verifier error with the last known AIML context

### AIML stacktrace tests

- a simple call chain `main -> a -> b -> c`
- crash in `c`
- check that `stderr` shows the AIML frames in the right order
- recursion/multiple return cases
- throw/catch paths

### Parity tests

- JIT run
- AOT run
- an identical AIML stacktrace in both modes for the same repro

### Negative tests

- a crash outside AIML code: only the native stacktrace, the AIML stack possibly empty
- an empty frame stack must not break the crash handler itself

## Risks and open decisions

### 1. Signal safety vs. comfort

A nicely formatted AIML stacktrace is worthless if it crashes again in the crash handler itself.

Decision:

- robustness before beauty

### 2. Frame lifetime with exceptions

If push/pop is not symmetric across all exit paths, the AIML stacktrace is unusable.

Decision:

- no scattered ad-hoc pops
- prefer centralised epilogue/clean-up paths

### 3. Host threads and callbacks

If AIML functions later run on worker threads or via callbacks, the frame management must respect that.

Decision:

- `thread_local` from the start

### 4. Source loc completeness

AIML frames are only as good as their `SourceLoc` data.

Decision:

- first make the AST propagation really clean before building a lot of frame logic

## Recommended order

1. Phase 1: global native crash handler
2. Phase 2: `SourceLoc`
3. Phase 3: AST propagation
4. Phase 4: centralise compiler diagnostics
5. Phase 5: runtime frame stack
6. Phase 6: compiler instrumentation
7. Phase 7: combined crash dump
8. Phase 8: symbolisation / build integration

## Minimal first vertical slice

If only the smallest useful block is to be built first:

1. centralise the global native crash handler
2. introduce `SourceLoc`
3. rebuild a single semantic error path via `ctx.error(node, msg)`
4. instrument the AIML frame stack for normal function calls
5. also output the AIML frames on `SIGSEGV`

That way AIML would early on already have:

- usable native `stderr` crash output
- the first real AIML frames
- proof that the architecture works end to end

## Implementation cut as subtasks

So that `sprint_task_16` does not end up as a single giant block, the work is cut into sequential subtasks.

### sprint_task_16_1

Title: centralise the global native crash handler

Scope:

- introduce a central crash diagnostics component
- move the signal handler from `mirror_runtime.cpp` onto the official process path
- connect `std::set_terminate(...)`

Affected files:

- `main.cpp`
- `src/sys/mirror_runtime.cpp`
- a new block such as `src/core/crash_diagnostics.{h,cpp}`

Acceptance criteria:

- `SIGSEGV` and `SIGABRT` print a native stacktrace on `stderr`
- mirror no longer installs a separate special path

### sprint_task_16_2

Title: introduce `SourceLoc` as the official source location abstraction

Scope:

- define the `SourceLoc` struct
- build AST/compiler helpers for file/line/column
- normalise relative paths to the project root

Affected files:

- `src/core/ast_nodes.h`
- `src/core/context.h`
- `compiler.cpp`

Acceptance criteria:

- core AST nodes can carry a normalised source location
- `sourceLine` is no longer the only source location API

### sprint_task_16_3

Title: switch the AST builder and parse paths to complete source locations

Scope:

- annotate expressions, statements and top-level declarations consistently
- define fallback rules for generated helper nodes

Affected files:

- `src/core/ast_builder.cpp`
- builder sub-files in `src/core/ast/`

Acceptance criteria:

- function calls, member accesses, returns, throws and top-level decls carry usable locations

### sprint_task_16_4

Title: centralise compiler diagnostics

Scope:

- introduce `ctx.error(node, msg)` / `ctx.error(loc, msg)`
- switch frequent semantic errors to the new API
- improve the parser/import/verifier context

Affected files:

- `src/core/context.h`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler_call.cpp`
- `compiler.cpp`

Acceptance criteria:

- typical compiler errors show the AIML file/line/column instead of only C++ log locations

### sprint_task_16_5

Title: introduce the AIML frame stack in the runtime

Scope:

- `thread_local` AIML frame storage
- runtime API for `push_frame`, `pop_frame`, `dump_stacktrace_fd`
- a robust, signal-poor frame data structure

Affected files:

- `exec_impl.cpp`
- a new runtime block such as `src/core/aiml_stacktrace.{h,cpp}`

Acceptance criteria:

- the runtime can dump the current AIML frame stack independently of the native stack

### sprint_task_16_6

Title: compiler instrumentation for AIML frames in JIT and AOT

Scope:

- emit push/pop on AIML function calls
- make return/throw/exceptional paths symmetric
- align wrappers and entry points

Affected files:

- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_call.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `compiler.cpp`

Acceptance criteria:

- JIT and AOT maintain the same AIML frame stack style
- no frame leak remains after a normal run

### sprint_task_16_7

Title: output a combined native plus AIML crash dump

Scope:

- merge the crash handler and the AIML frame dump
- finalise the `stderr` format
- secure the behaviour with an empty AIML stack

Affected files:

- `src/core/crash_diagnostics.{h,cpp}`
- `src/core/aiml_stacktrace.{h,cpp}`
- `main.cpp`

Acceptance criteria:

- a crash in AIML code shows the native and AIML stacktrace on `stderr`

### sprint_task_16_8

Title: complete symbolisation, build integration and repro tests

Scope:

- secure debug symbols for sensible native backtraces
- add JIT/AOT repros and crash tests
- complete the documentation for debug vs release behaviour

Affected files:

- build/run scripts
- `tests/`
- possibly docs in `aiml.md` or a separate runtime doc

Acceptance criteria:

- native backtraces are readable in the debug build
- targeted repros cover `SIGSEGV`, `terminate`, compiler diagnostics and AIML frames

## Recommended order of the subtasks

1. `sprint_task_16_1`
2. `sprint_task_16_2`
3. `sprint_task_16_3`
4. `sprint_task_16_4`
5. `sprint_task_16_5`
6. `sprint_task_16_6`
7. `sprint_task_16_7`
8. `sprint_task_16_8`

This order is deliberately strict:

- first the native crash path
- then source locations
- then compiler diagnostics
- only afterwards AIML frames and their instrumentation

## Done definition

The work is only really done when all of the following points hold:

- native fatal signals centrally print a backtrace on `stderr`
- uncaught C++ exceptions print a `terminate` trace
- typical compiler diagnostics show the AIML file/line/column
- JIT and AOT deliver the same AIML stacktrace style for the same repro
- mirror and other runtime parts no longer use a special path for crash handling
- the diagnostic/stacktrace architecture is documented and replaces old planning documents
