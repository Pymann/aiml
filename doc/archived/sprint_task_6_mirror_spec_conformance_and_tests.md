# sprint_task_6

## Title

Secure mirror semantics against the current runtime and repros

## Status

Still relevant, but the question is more concrete today than in the first version.

## Goal

The mirror implementation should be stabilised for the real problem zones: typed registration, delta sync, event/RPC queues, WS/TCP behaviour and real app repros such as `9rooms`.

## Current state

- Mirror is productively usable and already broadly exposed in the compiler.
- `poll_event` and `poll_rpc` exist today; the old wording as a mere API gap is therefore too weak.
- Real failure patterns already live in [mirror_ws_crash_report.md](../../mirror_ws_crash_report.md) and WS tests.

## What was outdated in the old version

- Poll/wait ergonomics as hypothetical follow-up work is too general.
- The main value of the task today lies in regressions for real mirror semantics, not in an abstract spec comparison alone.

## Focus

1. Typed register/unregister and delta sync.
2. Event/RPC queue semantics.
3. WS and TCP parity in the critical cases.
4. App-level repros, especially `9rooms`.

## Affected places

- [spec/44_5_mirror.md](../../spec/44_5_mirror.md)
- [mirror_ws_crash_report.md](../../mirror_ws_crash_report.md)
- [compiler.cpp](../../compiler.cpp)
- [src/sys/mirror_runtime.cpp](../../src/sys/mirror_runtime.cpp)
- [tests](../../tests)

## Acceptance criteria

- The real mirror semantics are secured testably for the most important cases.
- WS and TCP paths do not silently diverge in core cases.
- Real repros are kept as regression tests.
