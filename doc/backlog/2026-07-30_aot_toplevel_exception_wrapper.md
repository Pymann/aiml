# AOT build: top-level handler for uncaught exceptions

Date: 2026-07-30
Status: backlog

## Context

On 2026-07-30 **JIT** execution (`aiml … -r`) was cleaned up: an
uncaught AIML exception is now caught at the top level in `AIMLCompiler::jitRun` by
a `try/catch(...)` → prints `Unhandled AIML exception: <message>`
to stderr + `return 2` (instead of `std::terminate`/core dump). The `message` is
stored in a thread-local in `visitThrow` before `__cxa_throw` via `aiml_note_thrown_message`
(see memory `aiml_exception_propagation`).

**Open:** The **AOT** build (native executable, e.g. the 9rooms `game` server via
`exodus build -c …`) has NO equivalent wrapper. The compiled AIML `main`
is the entry point; an uncaught exception unwinds out of `main` →
hard abort (`std::terminate`) without a diagnostic. For a long-lived server
that is dangerous (a single bad-data path kills the process).

## Plan

- In AOT codegen do NOT export the AIML `main` directly as the C `main`, but
  generate/link a C++ `main` wrapper that calls the AIML `main` in
  `try { … } catch (...) { fprintf(stderr, "Unhandled AIML exception: %s\n",
  g_aiml_last_thrown_msg…); return 2; }` — analogous to `jitRun`.
  `aiml_note_thrown_message` + `g_aiml_last_thrown_msg` already exist
  (`compiler.cpp`); for AOT they have to move into the runtime (`libaiml`) instead of the
  compiler, so that the linked binary finds them.
- Alternatively: register a `std::set_terminate` handler that prints the last noted
  message.

## Why it matters

Blocks clean server-side error handling for the planned
effect/cost cascade validation (9rooms, separate backlog): there the
create_card RPC handler MUST catch the exception itself, because otherwise the AOT server
crashes. An AOT top-level wrapper would be the more robust net.
