# Borrow escape: compiler analysis and implementation package

As of: 2026-09-11. Analysis assignment, no compiler change. No git used.

## Result

The 14 messages from the 9rooms_v2 server build arise in alternative initialisation
methods that are compiled along. The ServerRuntime uses classgroup
constructors, `init_bare`, `sees` and `on_wired`. An exemption only for
classgroup wirings does not remove the warnings in the base methods.

The current check is syntactic and at the same time too strict and incomplete:
it also warns for `consumed`, but does not recognise passing on via a local
alias. A reproduction with an alias, an explicit `free` and a subsequent
read compiles without a borrow warning; Valgrind demonstrates the invalid access.

## Concrete integration points

| Location | Current function | Consequence |
|---|---|---|
| `src/core/ast_compiler_stmt.cpp:885`, `visitSetStmt` | Immediate warning for `this.member = ptr/ref parameter` | Replace by structured findings; take the parameter qualification and the actual assignment path into account. |
| `src/core/ast_compiler.cpp:4554`, `visitFunction` | Sets `is_param` also for consuming parameters; then reads `argMods` into `sym.lifetime` | `is_param` does not automatically mean borrow. |
| `src/core/ast_compiler.cpp:4746` | Creates an `OwnershipPlan` per function | No existing project-wide lifetime proof. |
| `src/core/ownership_analyzer.cpp:36,106,198` | Recognises selected value locals to be freed; calls and other statements do not change ownership facts here | Do not use unchecked as a safety proof for stored pointers. |
| `src/core/ast_compiler_expr.cpp:132` | `new` allocates via `calloc` | Currently no general automatic box drop. |
| `src/core/ast_compiler_stmt.cpp:338,1680` | Registers/emits drops for certain values, strings and lists; explicit `ptr/ref` are excluded from model drops | The lifetime of a box pointee is currently not the scope lifetime of the pointer variable. |
| `src/core/ast_compiler_call.cpp:377` | Explicit `free` | Must invalidate a later borrow provenance. |
| `src/core/ast_compiler_call.cpp:403,667,2485` | Free calls with the hot-reload table, interface dispatch, concrete/inherited methods | Record resolved semantic targets at these places; the LLVM `call` alone is not enough. |
| `src/core/ast_compiler.cpp:1250,2431` | Classgroup check and synthesis before body codegen | Keep owner/port relations as metadata during synthesis. |
| `src/core/context.h:389` | The registry so far contains container names | No finished instance/owner relation available. |
| `compiler.cpp:929,1202,1244` | Import header, separate import compilation, then root codegen | The analysis must connect translation units. |
| `compiler.cpp:3238,3281` | A cache hit skips compilation; imports get their own compiler instances | Never treat missing findings on cache hits as proof of safety/unreachability. |
| `compiler.cpp:1246,3327`, `compiler.h:91` | Existing merging of prefab/enum findings | An architectural model for project-wide borrow findings, not a finished call graph. |

## Memory management: correction of the earlier reasoning

`AIML_LIFETIME_DROPS` is off by default in the compiler (`ast_compiler.cpp:1222`).
The 9rooms game build does not set it explicitly in `exodus-game.json`; an
inherited environment can still switch it on. The client sets it explicitly
to 1. The memory test driver activates it as well.

Even with the switch activated, the generated IR of the existing borrow
test contains two `calloc` calls and no corresponding box drop. `g_runtime = runtime`
currently keeps a pointer to the runtime; from that one must not infer an already
implemented recursive RAII destruction of the whole service graph.

That supports classifying the reported server places as UAF false alarms in the
current path. It is not a general proof that all 9rooms memory accesses
are safe. In particular, the new analysis must not mark all `new` objects as permanently safe
merely because automatic release is currently missing.

## Checks actually run

Results and isolated reproductions: `/tmp/aiml-borrow-audit-20260911/`.
The existing binary `out/aiml/aiml` was used, no compiler rebuild.

| Check | Result |
|---|---|
| `borrow_escape_warn`, `classgroup_wiring`, `classgroup_sees_base_member`, `classgroup_post_wire`, `classgroup_subgroup_port`, `classgroup_cross_unit`, each with drops 0 and 1 | 12/12 runs exit 0. Borrow test one warning each; classgroup tests none. |
| `consumed_store.aiml`: the parameter gets `lifetime: consumed` | Compiles, still a wrong borrow warning. |
| `alias_store.aiml`: store the parameter via a local pointer variable | Compiles, no borrow warning. |
| `unused_store.aiml`: call removed completely | Compiles, still a borrow warning. |
| `alias_uaf.aiml`: store an alias, free the pointee explicitly, read the member | Compiles without a borrow warning; Valgrind exit 99, `Invalid read of size 4`. |
| Existing safe borrow test under Valgrind | Exit 0, `ERROR SUMMARY: 0 errors`. |
| Fresh 9rooms game compile to `/tmp/.../server/main.ll`, drops explicitly 0, strict top level 1, asset roots `assets` | Exit 0; exactly 14 borrow warnings; 186 IR files including imports. Server not started. |

The server IR calls `ServerRuntime___ctor`; this uses
`EffectRuntime___ctor`, `LobbyService_init_bare` and `MatchService_init_bare`.
Across all 186 IR files there are no direct calls of
`DeckService_wire_cards`, `LobbyService_init` or `MatchService_init`. The three
manual effect/trigger/reaction wiring calls have only
`MatchService_init` as their direct caller. That is a check of direct calls,
not a complete reachability proof in the presence of arbitrary dynamic calls.
In `init_server_state` and the two runtime constructors there are no
`free`/`decref`/`free_decoded` calls; `runtime` is stored in `g_runtime`.

## Test suites: significance and gaps

- `tests/run_memory_sanitizers.sh` uses Valgrind, not ASan. A deliberate
  double free checks that the instrumentation actually sees errors.
  Correctness and memory accesses are checked separately; leaks are not an
  error criterion. Without Valgrind the script reports SKIP with exit 0.
- The borrow probe call at line 162 checks neither presence nor absence
  of the warning. The fixture does not even read the stored pointer back.
  The consuming parameter test likewise declares several methods without
  calling them. Both are insufficient as the sole diagnostic/semantic acceptance.
- `run_all_tests.sh` mostly checks exit codes, excludes `_...` subdirectories
  and therefore does not run `_mem_sanitize`. At the end an explicit
  error exit based on `FAILED` is missing; the last output can return exit 0
  despite failures. In addition the script deletes cache and DB directories. It was not started for this
  analysis.
- Classgroup tests cover wiring, base members, post-wire, subgroups,
  ports, interfaces and cross-unit use; they provide no general
  ownership or release proof.
- 9rooms `run_relevant_tests.sh` covers the affected cluster, but only checks
  runtime success. Missing files are printed as MISS without increasing `fail`.
  For a binding acceptance, completeness must be checked as well.
- The complete compiler, memory and 9rooms suites were not run here.
  The twelve runs are a focused baseline.

## Revised implementation order

### A. Diagnostic contract and reliable regressions

Add a dedicated diagnostic test driver: expected exit, diagnostic ID,
presence/absence, count and source location. Do not just search for the word PASS.
Take over the reproductions above; only compile real UAF repros or
start them in a controlled way under Valgrind. Distinguish `consumed` from borrow.

`spec/61` must explicitly distinguish between a proven lifetime contract, an
unknown borrow escape and a proven invalid access.
Do not change automatic drops, copies or ownership transfers through this diagnostic step.

### B. Independent borrow findings and method summaries

A new LLVM-independent `borrow_analysis.h/.cpp` instead of extending the drop gate into a
universal borrow checker. Stable keys from module, qualified method
and parameter position; the stored member path and source location as values. Do not
store raw AST pointers beyond the lifetime of a sub-compiler instance.

Capture direct parameter stores, local aliases and forwarding to other methods.
Merge branches/loops, recursive calls via a fixed point;
unknown constructs stay unknown. Evaluate the summaries at the end,
independently of the order of the method definitions.

### C. Project-wide context including the cache

Establish an analysis context shared per build in `AIMLCompiler` and pass it on into
`resolveImport`. Capture findings via the existing header AST path even
on cache hits, or introduce versioned analysis sidecars. For the
first version the header path is preferable: it is read for types anyway.
Deduplicate synthesised/moved methods and duplicate imports along the way.

Object/IR output, JIT with a cold and a warm cache and isolated library
compilation must deliver consistent diagnostics. With an incomplete analysis
the warning stays. No unconditional suppression in `libraryMode`.

### D. Bounded proofs and reachability

1. Track pointer origin and actual object identity; the same class,
   a global pointer variable or a shared scope alone are no proof.
2. Store classgroup instance, subgroup and port relations during synthesis.
   Distinguish different instances of the same runtime type.
3. Check safe, locally completely visible uses as well as unchanged shared
   owners. Explicit `free`, rebinding, unknown mutations and
   external calls invalidate proofs. Unsupported cases stay warnings.
4. Hide unreachable methods only in a provably closed program.
   Roots include the entry, module initialisation, constructors, hooks and reachable
   callback/interface/dynamic registrations. Capture calls known in the source language
   before hot-reload lowering. Unknown dynamic use
   or an open library API must not yield a false proof of unreachability.

For the 14 concrete 9rooms messages point 4 is central: the reported
base methods are bypassed by classgroup wiring. Lifetime metadata
alone does not resolve their definition-side warnings.

### E. Acceptance

- Safe and unsafe callers of the same method, parameter aliases, transitive
  passing on, recursion, branches, scope end, explicit free and owner change.
- Inheritance, interface calls, callback reachability, external library use.
- Two separate runtime instances, subgroup port, globally stored runtime.
- Single-file and cross-unit cases with import order/diamond-shaped imports,
  cold/warm cache, JIT and object/IR output.
- A warning change must not change codegen and memory management behaviour;
  compare IR structurally and the relevant runtime tests.
- The 14 9rooms warnings may only disappear after documented proof.
  Open dynamic cases stay visible, even if the goal is otherwise missed.
- After that the dedicated diagnostic driver, the classgroup selection, the complete memory
  suite, the 9rooms cluster and a fresh game build. Only count complete suites as acceptance
  with reliable error/skip evaluation.
