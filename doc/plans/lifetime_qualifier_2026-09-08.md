# AIML `lifetime` — ownership qualifier (design → spec)

As of: 2026-09-08. Status: design, fixed by the user; the basis for the
compiler implementation. Replaces the discarded "WP-C escape analysis" approach
(pseudo-GC, opaque) by **explicit, compile-time single-owner semantics**.

## 1. Motivation

The memory leak arises because heap-carrying values (string/list/map/model) are not
freed at scope end or on overwrite. The solution is **no GC**
(runtime reachability, opaque) and **no escape analysis** (whole-program inference,
not locally readable for humans/AI), but **ownership**: a value has exactly one
owner, whose death (scope end / overwrite / container drop) frees the heap
deterministically at compile time. Transfers are **syntactically visible**.

Efficiency: everything at compile time; `free` at a known place (like a C++ destructor/Rust
drop), **no** runtime tracing, **no** refcount. Move = pointer transfer (zero-copy).
This replaces "never free" (OOM) by bounded memory — more efficient overall.

## 2. The qualifier

On the **variable decl** (like existing decl qualifiers, e.g. `access`):

```
- decl: [x, MatchSession, { lifetime: scope }, <init>]     # scope = default, can be omitted
- decl: [buf, MatchSession, { lifetime: persistent }]      # outlives the scope
- decl: [h, FileHandle, { lifetime: consumed }, <init>]    # MUST be moved out
```

Values:

| Value | Meaning | Drop at scope end? |
|---|---|---|
| **`scope`** (default) | Owned; dies at scope end — unless consumed before. | yes, unless consumed |
| **`persistent`** | Owned, but bound to a longer-lived owner (member/box/static). | no |
| **`consumed`** | Linear obligation: MUST be moved out exactly once (coding aid). | no (the move is mandatory) |

Only heap-carrying values are affected. Scalars (i32/f32/i64/bool/enum) have no
heap → the qualifier is meaningless/ignored.

## 3. Ownership from type + initialiser (0 qualifiers for the normal case)

Ownership is already **locally visible** for ~all symbols, without a qualifier:

- **Value type `T`** (string/list/map/model) = **owned** (default `scope`).
- **`ptr<T>` from `at`/`get`/`&`/a parameter** = **borrow** — never owns, is never
  dropped, only valid within the owner's lifetime. **Borrows do not escape**
  (storing into a member/container = compile error → forces copy/move).
- **`ptr<T>` from `{new: T}`** = **owned box** (default `scope`: pointee freed at scope end).

So the qualifier is the **escape hatch** you only write when deviating
(`persistent`, `consumed`) — not per declaration.

## 4. Consumption: push / return / consuming functions

Agreement: **`push` and `return` always consume** (move). In general: every
**consuming function** takes ownership of its argument.

- A **`scope`** local that is consumed is **moved/dead** afterwards → **no** drop
  at scope end (the default `scope` is overridden by the consumption). Use-after-move
  = compile error.
- A **`persistent`** local is **exempt** from consumption: the consuming op
  **copies** instead (the original survives), and it is not dropped at scope end.
- A **`consumed`** local: see §5.

Copying counterparts stay explicit: `at_copy`, `push_from`, assignment = deep copy
(the source keeps its own and is dropped normally at scope end).

## 5. `consumed` = linear coding aid

`consumed` enforces that an owned value is **passed on** and not accidentally
dropped (e.g. a handle/resource that MUST be handed over):

- If a path reaches the scope end **without** having consumed the local → **warning**
  ("consumed value is only conditionally/not consumed").
- Double consumption → compile error (use-after-move).

It is a pure diagnostic aid; the runtime behaviour corresponds to a consumed `scope`.

## 6. No runtime drop flag → compile-time-unambiguous consumption

Deliberate decision: **no hidden runtime bools** (no "drop flag"). Consequence:
the consumption status must be unambiguous at compile time per control flow path.

- If a `scope` local is consumed differently in different branches and the
  branches merge, the drop cannot be emitted unambiguously without a flag → **diagnostic**:
  "conditional consumption — consume in all branches or in none" (or pull the consumption before the
  branch, or use a copy). For `consumed` it is a **warning**.
- That is a slightly stricter **compile rule** instead of runtime overhead — zero
  runtime cost, locally readable. Real code mostly consumes unconditionally or symmetrically.

## 7. What replaces today's broken middle

Today `at`/`get`/`set`/`push` share the heap by shallow copy (two owners) — the
reason why ownership could not be determined so far. New, unambiguous:

| Op | Semantics |
|---|---|
| `at`/`get` | borrow (`ptr<T>` view), does not own |
| `at_copy` | owned deep copy |
| `push` | move (source consumed) |
| `push_from` | deep copy into the container |
| assignment `set: [dst, src]` | deep copy (or move, if src is a consumable scope local); the old `dst` is dropped first |

## 8. Compiler tasks (implementation, gated by the sanitizer harness)

1. Determine ownership per symbol from type + initialiser + qualifier (local, no
   whole-program analysis).
2. Emit the drop (`aiml_ser_free_decoded` / `decref` / container drop) at scope exits for
   owned, non-consumed locals — per control flow path.
3. Treat consumption at `push`/`return`/consuming functions as a move; mark the source as
   moved; diagnose use-after-move.
4. `persistent`: no auto-drop; consumption → copy.
5. `consumed`: warning on an unconsumed path; error on double consumption.
6. Keep borrows scope-local (storing = error).
7. `set`/overwrite: RHS first, drop the old `dst`, then take over (self-assign safe).

Every iteration against `tests/run_memory_sanitizers.sh` (control detected + probes
memory-safe, "definitely lost" goes down, no new invalid free/UAF).

## 9. Delimitation

No GC, no refcount as the default (`shared` stays a separate, rare exception, if ever
needed — deliberately NOT part of this qualifier). No escape analysis. Existing
value semantics (by-ref lists/maps, by-value model/class args) stay; what is new is only the
deterministic release + the visible transfer rules.
