# AIML Language Review: Determinism, Semantics, and Clarity

Date: 2026-06-04
Status: review

## Summary

AIML has a strong and interesting direction: it combines a compact YAML-based syntax with a systems-oriented runtime, domain-specific capabilities, and a surprisingly rich safety discussion in the spec.

However, the current language does not yet fully achieve the goal of being a highly deterministic language in a global sense.

The best current characterization is:
- AIML has a partially deterministic core
- but it also exposes multiple intentionally stateful, concurrent, time-based, and runtime-shaped features
- and the semantic boundary between those two zones is not yet defined sharply enough

In short: the language is already quite expressive and often pragmatic, but its determinism story is still mixed, and several semantics remain softer or more implementation-shaped than ideal.

## Main Assessment

### 1. Determinism is present as a tendency, not as a hard language property

AIML behaves most deterministically when code stays inside a relatively constrained subset:
- local computation
- explicit control flow inside a `main` function
- value-oriented data manipulation
- no concurrency
- no hot reload
- no time/random/network interaction
- no alias-heavy pointer flows

Outside that subset, determinism weakens quickly.

Relevant examples:
- native threads: `spec/26_concurrency.md`
- thread-safety model with non-atomic multi-step sequences: `spec/47_thread_safety.md`
- time, sleep, and external-world interaction: `spec/24_time.md`
- mirror/network synchronization: `spec/44_5_mirror.md`
- hot reload and versioned code replacement: `spec/30_hot_reload.md`

This is not necessarily a design failure. It simply means AIML is currently not a deterministic language by default; it is a language with deterministic regions.

### 2. Too many important behaviors are still `undefined`, `unspecified`, or runtime-contract based

This is one of the main reasons the determinism story feels incomplete.

Examples:
- iterator use after `free` is undefined
- mutating a collection during iteration is undefined
- `Kv` iteration order is unspecified
- duplicate namespace/FQN cases are not fully nailed down
- hot reload plus in-flight exceptions is unresolved
- append-only layout evolution still leaves undefined behavior around older instances

These are all understandable engineering compromises, but in aggregate they create a language feel closer to “careful systems language with conventions” than “strictly defined deterministic language”.

### 3. `ref<T>`, `ptr<T>`, and `any` currently overpromise semantically

This is probably the biggest semantic clarity problem in the language today.

Right now:
- `ref<T>` and `ptr<T>` are still operationally the same in the current implementation
- `ref<T>` suggests borrow/lifetime rules that do not yet exist
- `any` currently allows raw pointer reinterpretation without enforced runtime validation

That makes the language look safer and more constrained than it really is.

This is especially important because AIML syntax is compact and approachable. The more friendly the syntax looks, the more important it is that semantic sharpness matches user expectation.

### 4. is solved
### 4. Top-level execution semantics are still too surprising

The spec already documents the problem very honestly: top-level imperative execution is subject to ordering surprises because of synthetic init lowering and declaration hoisting.

The static analysis chapter already introduces `AIML_STRICT_TOPLEVEL` as a mitigation, and explicitly notes that execution order inside a proper `main` function is deterministic.

That strongly suggests the current default semantics are not ideal.

If a language wants to feel deterministic and clear, source order should very rarely become surprising in ordinary code.

#### 4.1 JIT vs AOT — same code paths, same behavior

Both modes call the same `generateEntryPoint()` and build the same C `main`:
`setvbuf → trace_init → __aiml_init() → __aiml_user_main()`.

Top-level execution semantics are therefore **identical between JIT and AOT**.
Differences (`AIML_TRACE` bake, `sys.exec`/Hot-Reload availability, dynamic
library loading) are orthogonal to the ordering question.

#### 4.2 Main-file vs Imported-file — RESOLVED 2026-06-04

> **Update (2026-06-04):** the asymmetry below has been fixed in
> [src/core/ast_compiler.cpp](../../src/core/ast_compiler.cpp).  Both
> branches now share the same Phase-1/2/3 layout (storage → container
> init prologue → in-source-order visit).  The remaining text in this
> section is preserved for historical context; the bullet-list at the
> end no longer applies.  See [spec/08_entrypoint.md](../../spec/08_entrypoint.md#top-level-execution-order)
> for the normative spec, and `tests/test_import_toplevel_order.aiml`
> for the regression test.

The real asymmetry **was** between the main file and imported library
files:

- **Main file** (non-`libraryMode` branch in `compile()`):
  - Pre-pass allocates `GlobalVariable` storage only.
  - Visit-pass walks all top-level statements **in source order**,
    `visitDeclStmt` emits the initializer in-place.
  - `decl: [x, T, {call: ...}]` followed by `call: [...]` runs in source order.

- **Imported file** (`libraryMode` branch in `compile()`):
  - Pass 1 walks **only `DeclStmt`s** and emits both storage *and* the
    initializer immediately, in source order of decls.
  - Pass 2 walks the **non-decl statements** afterwards.
  - Result: `decl A; call B; decl C; call D;` was reordered to
    `init_A; init_C; B; D`.

This was introduced by commit `4a5eb1c` ("critical fixes", 2026-03-16) for
imported libraries.  Commit `ddc718b4b` ("critical fixes", 2026-03-22)
later refactored the **main-file** path into a proper "storage-only
pre-pass + in-order visit-pass" — but **forgot to apply the same fix to
libraryMode**.  There was no triftiger technical reason for the
asymmetry; it was an oversight, evidenced by:

- the inline code comment on the main-file pre-pass explicitly said
  "Only the storage is created here; initializers are emitted in-order
  below via visitDeclStmt"
- no test exercised ordering inside an imported library
- the libraryMode comment made no claim about ordering

The bug was observable in real code (this session: discovergen_boot.aiml
where `decl: [vocab_ok, i32, {call: [load_vocabs]}]` ran *before*
preceding `call: [set_path, ...]` statements).

A first migration attempt during this session hit a **second, deeper
bug**: when a library declared a `list<T>`/`map<K:V>` global and a
same-file function referenced it, the function call inside the same
library's `__aiml_lib_init_*` ran *before* the runtime container init
had been ordered into the same lib-init function — segfaulting on the
uninitialised data pointer.  The final fix added a **container-init
prologue (Phase 2)** that emits `initList`/`initMap`/`ChanImpl::init`/
`BufferImpl::alloc` for every container global *before* any
user-code emission begins.  This is conceptually equivalent to the
`llvm.global_ctors` option originally considered, but implemented as a
prologue inside the same lib-init function so it benefits from the
existing eager-load ordering.

In parallel, the **void-call-as-initializer** failure mode (a
`decl: [x, T, {call: [void_fn]}]` triggers a stack overflow inside
LLVM's DataLayout when the malformed IR is laid out) was added as a
hard AST-level compile error; see
[spec/49_static_analysis.md §12](../../spec/49_static_analysis.md).

Pre-fix migration advice (no longer required):

- ~~**prefer `main`-centric code** (see `AIML_STRICT_TOPLEVEL=1`)~~
- ~~**avoid `decl: [x, T, {call: [...]}]` in imported libraries**;
  use an explicit `init()` function and call it from main instead~~

### 5. Some advanced features still leak implementation details too directly

Mirror, DB integration, pointer/view semantics, and serialization are powerful, but several parts still expose the shape of the underlying runtime too directly.

Examples:
- `Kv`/`map` synchronization in Mirror v1 depends on pointer-stable dirty detection limitations
- zero-copy view semantics in DB are useful, but they also require users to understand aliasing and cache identity in detail
- iterator rules are effectively inherited from the underlying C++ container model

These features are impressive, but they are not yet fully “language-smoothed”.

### 6. The spec is rich, but not yet uniformly normative

Some chapters are crisp and language-like.
Others still read more like:
- design notes
- runtime architecture memos
- implementation commentary
- migration guidance

That is useful during active language design, but it makes it harder to distinguish:
- what AIML guarantees
- what the current implementation happens to do
- what is only planned
- what is recommended style rather than semantics

The language would benefit from a stronger separation between:
- normative spec
- rationale
- implementation notes
- future work

## What Already Works Well

Despite the criticism above, a lot is going in the right direction.

### 1. The language is willing to encode semantics explicitly

AIML already shows a strong habit of making semantics visible instead of hiding them completely:
- `ptr<T>` versus value declarations
- explicit `kv.get` versus `kv.get_copy`
- explicit `sync:` and `rpc:` model annotations
- explicit thread-safety categories
- explicit strict-analysis switches

This is a very good foundation.

### 2. The spec often acknowledges real failure modes instead of pretending they do not exist

The spec is refreshingly honest about:
- aliasing traps
- migration constraints
- hot reload limitations
- thread-safety boundaries
- runtime tradeoffs

That honesty is a strength. It makes the remaining ambiguity fixable.

### 3. The language already has the beginnings of a safer subset

There is a visible “clean AIML style” emerging:
- move imperative startup into `main`
- prefer explicit copy/view distinctions
- use channels or locks instead of casual shared globals
- avoid long-lived aliasing unless intentional
- keep data-centric models simple

This suggests a future deterministic/profiled subset is realistic.

## Recommended Direction

### 1. Define a formal “deterministic core” profile

This would help a lot.

For example, AIML could explicitly define:
- `deterministic core`
- `runtime/system profile`
- `unsafe/interop profile`

The deterministic core would exclude or constrain:
- threads
- time.now
- random
- hot reload
- external I/O
- unspecified iteration order
- raw `any` pointer reinterpretation

That would immediately make the language story much clearer.

### 2. Reduce `undefined behavior` in normal user-facing features

Undefined behavior should be rare and reserved for truly low-level escape hatches.

For mainstream language constructs, prefer one of:
- compile-time rejection
- runtime exception
- explicitly unspecified but bounded behavior

Especially important candidates:
- iterator invalidation
- namespace collisions
- hot reload interaction rules
- null pointer behavior
- map missing-key behavior

### 3. Make the `ref<T>` / `ptr<T>` status brutally explicit

Until borrow checking exists, the language should avoid implying more safety than it enforces.

Possible directions:
- clearly mark `ref<T>` as documentary-only in the formal spec
- or temporarily collapse the distinction in user-facing guidance
- or fast-track the minimal first borrow restrictions that make `ref<T>` semantically real

### 4. Make strict top-level semantics the long-term default

The `main`-centric model is much clearer and more deterministic.

The current opt-in strictness is a good migration bridge, but long-term the cleaner rule should likely be the standard rule.

### 5. Separate normative spec from implementation notes

A useful structure could be:
- normative chapter
- rationale
- implementation notes
- open issues / future work

This would improve clarity immediately without changing the language itself.

## Bottom Line

AIML is already a compelling and unusually capable language design, especially for game/runtime/AI-oriented systems work.

But if the target is “as deterministic as reasonably possible”, then the answer is currently:
- partially yes in the core
- not yet consistently yes across the language as a whole

The biggest gaps are not expressiveness, but semantic sharpness:
- too many soft contracts
- too many implementation-shaped behaviors exposed directly
- too much mismatch between apparent safety and actual enforced safety in a few core abstractions

The good news is that the language is already close to having a strong deterministic identity if it explicitly defines and hardens that subset.
