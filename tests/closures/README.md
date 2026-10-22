# Block / closure lab — spec/64 acceptance target

Self-verifying probes for `spec/64_closures.md` (PROPOSAL). Each positive probe declares its own
`callable:` signatures and types, exercises one group of rules, checks the result and returns
0 (PASS) / 1 (FAIL). Each `neg/` probe must be **rejected at compile time** with the message
named in its `# EXPECT-ERROR:` header.

**Status: written, never compiled, never run.** Neither kind exists in the compiler yet — these
are the acceptance target (spec and tests first, Arbeitsprinzip 3), not verified GREEN results.
They are expected to be RED until spec/64 is implemented.

```sh
bash tests/closures/run.sh        # all probes + the neg section
ONLY=03 bash tests/closures/run.sh
```

The compiler itself is always built with `exodus build --all` from the exvenv; the runner only
runs the probes with the built binary.

## The two kinds, in one line each

- **`block<Sig>`** — a body handed to a function that calls it and forgets it. Implicit capture of
  the live frame, mutations hit the caller's own locals, no allocation, no ownership, statically
  forbidden from escaping.
- **`closure<Sig>`** — a callable that outlives its birth scope. Explicit `capture:` list, owning
  heap environment, move-only (spec/61 Class 4).

## Positive probes

| Probe | spec/64 | Covers |
|---|---|---|
| `01_block_basics.aiml` | §2, §3 | `callable:` declaration; block as the **last argument**; implicit capture mutating the caller's locals; `args:` omitted → names from the signature; structural signature identity (`Each` == `Each2`); a block parameter passed on to another block parameter; a block with a return value; a block bound to a local and called repeatedly; nested blocks; `throw` out of a block reaching its caller |
| `02_closure_create_call.aiml` | §2, §4, §5 | the `closure:` expression; signature forms (no args / no return / both / nested generic); call as statement and expression; repeated calls (a call **borrows**); `args:` omitted and written out; a capture shadowing a global; `capture:` omitted; null closure; block vs flow YAML |
| `03_capture_modes.aiml` | §4.1, §4.2 | all four modes — `value` (scalar copied, container **aliased**, string incref'd, model member-wise), `copy` (shallow), `clone` (deep), `move` (the only legal `ptr<T>` capture) — plus the decl-shaped forms `[n, T, init]` and `[n, T, init, {mode}]` |
| `04_instances_state.aiml` | §6.1–§6.3 | two closure instances stay independent; a factory's captures survive the factory; closure capture mutation persists per instance and is **invisible outside**, while the same write **through a block** hits the enclosing variable; a captured `shared<T>` **is** mutually visible; nested closures; nested scopes / shadowing |
| `05_ownership.aiml` | §5, §6 | `ref<closure<Sig>>` = borrow; by-value parameter = move-in sink (`consumed`); return = move out (`producer`); move on assignment; `list<closure<Sig>>`; a closure field makes a model move-only; early `free:`; `throw` out of a closure body; null call raises; loop create/drop |

## Negative probes (§9 diagnostics)

| Probe | Expected error |
|---|---|
| `n01_block_returned` | `block-escapes` — a block returned from its frame |
| `n02_block_stored` | `block-escapes` — a block pushed into a container |
| `n03_block_captured_by_closure` | `closure-borrowed-capture` — a closure capturing a block |
| `n04_block_nonlocal_break` | `block-nonlocal-control` — `break` across the block boundary |
| `n05_unknown_signature` | `callable-unknown-signature` |
| `n06_signature_mismatch` | `callable-signature-mismatch` |
| `n07_closure_free_variable` | `closure-free-variable` — no implicit capture in a `closure:` |
| `n08_capture_unknown` | `closure-capture-unknown` |
| `n09_duplicate_capture` | `closure-duplicate-capture` |
| `n10_borrowed_capture_ref` | `closure-borrowed-capture` — a `ref<T>` captured |
| `n11_move_required` | `closure-move-required` — `ptr<T>` without `mode: move` |
| `n12_copy_forbidden` | `closure-copy-forbidden` |
| `n13_fn_mismatch` | `closure-fn-mismatch` |
| `n14_not_transferable` | `callable-not-transferable` — sent through a `chan` |
| `n15_capture_move_source_use` | `use-after-move` — source of a `mode: move` capture |
| `n16_closure_use_after_move` | `use-after-move` — closure used after a sink took it |

**One diagnostic has no probe: `callable-args-required`** (§3.1 — `args:` omitted where the
target signature cannot be inferred). Every position in which AIML accepts a value today is
typed: a `decl` always carries a type, an argument position has a parameter type, a `return:` has
the function's return type. So the rule has no reachable violation to probe, and the diagnostic is
a guard only. **Open for review**: either drop it from §9, or keep it and name the untyped
position it is meant to catch.

## Not covered here, deliberately

- **Engine callback (§7).** `engine.run` with a `block<Frame>` opens a window and would block a
  test loop, so it is **not** in this suite. It is a separate manual / headless-wasm check: the
  callback reaches its app state with no app global, and browser suspension and shutdown do not
  release anything early.
- **Native vs wasm32 agreement (§7, criterion 17).** Run `01`–`05` through the wasm target
  (`exodus-wasm` + emsdk-node) and compare output; that is the only way wasm32-only faults show.
- **Leak gates.** Green ≠ leak-free. These probes must additionally run under the lifetime leak
  gates / valgrind before any closure memory claim is made.

## Driver convention (17.10.2026)

This lab is now a **driver** under the existing `tests/<dir>/run.sh` convention: the corpus run
invokes `tests/closures/run.sh` as a single entry and takes its exit code, and every probe carries
`# Requires: driver` so the main suite skips the individual files **visibly** instead of filtering
them out silently.

The lab stays RED by design until spec/64 is implemented — but it is now **one** honest red line
instead of 21 scattered ones. If a green corpus matters before that, the `STAGE` mechanism of
`tests/clearance/` is the pattern to copy: each probe names the implementation step it needs, and
the runner only grades the step that has been built.
