# Implementation plan: spec 61 ownership for call temporaries

As of: 02.10.2026, implemented after review on the basis of `572603f`. The historical line numbers below refer to the planning state; function names remain the stable anchors. The user explicitly excluded tests and 9rooms runs and specified `exodus build --all` in the AIML directory as the only check.

## Implementation state

- `ast_compiler.cpp`: forward signatures are completed even for existing LLVM declarations; existing additional metadata is kept. `this=ref<Class>` was already corrected in the base commit. RC parameters with `consumed` hold a callee reference of their own; the caller reference remains.
- `ast_compiler_call.cpp` / `.h`: a shared `ResolvedCallSignature` for free functions, methods including field chains and inheritance, interface methods and `fn<...>` variables. The type query generates no receiver IR. All four call routes use the shared argument completion, including interface void/sret and indirect calls.
- `context.h` / `ast_compiler_stmt.cpp`: typed temp frames with an entry alloca per temp site. Null initialisation is inserted at the start of the statement and at every while condition. Ownership transfers and releases reset the slot. Unwind pads take into account the temp slots already created in addition to the local snapshot; cache keys contain their addresses and drop kinds. Slots created later in the same statement only belong to later pads, since they cannot yet hold an owner at earlier throws.
- String producers in `ast_compiler_expr.cpp`, `ast_compiler_call.cpp`, `builtin_registry.cpp` and `serialize_impl.cpp` are connected to the same temp slots. Container, model and unique box arguments are registered right after their evaluation, before the next argument is evaluated. Unique boxes are transferred to the callee at the call.

Validation: `exodus build --all` in the AIML directory completed successfully for the final state (exit code 0; build log: `/tmp/aiml-spec61-build-final.log`). 44 regression test programs including an import fixture were added under `tests/lifetime/call_temporaries/` and integrated via a shared manifest into the functional lifetime suite, the AOT leak gate and the memory safety suite. Coverage and status are in `tests/lifetime/call_temporaries/README.md`. The new test programs were neither compiled nor run. No tests, leak gates, sanitizer runs or 9rooms builds/measurements were run. The acceptance criteria described below are therefore open; no leak pattern is marked GREEN based on the build check alone.


### Acceptance (2026-10-02, addendum)

All 44 regressions compiled and run under Valgrind: exit 0, 0 definitely/indirectly lost
bytes, no memory errors. This required test corrections (valid AIML, see README) and three
compiler fixes: `fn<…>` calls pass the address for `ref<T>`/`ptr<T>` parameters; the `ptr<T>` move
only takes effect on entering the callee (the local's slot is nulled after argument evaluation,
instead of removing the drop at compile time) — an argument throwing before that leaves the box with the
caller; `spawn` only moves after argument evaluation. In addition the borrow checker recognises
cross-loop use-after-move and moves to method/interface parameters (neg n20/n21).

## Goal and delimitation

Fix all six confirmed failure patterns: method forward reference, producer via a nested receiver, producer via a function pointer, interface consumer, function pointer consumer and a temporary caller owner on an exception in the consumer. spec/61_memory_management.md takes precedence, especially §§5, 8, 9.2 and 9.3. The valid original expression in 9rooms must become leak-free without an additional local variable.

No change of the language contract and no additional mandatory annotations in 9rooms. Container arguments keep their RC semantics. `ref<list<T>>` is not a solution. `consumed` on RC types does not mean across the board that the caller reference disappears: the consumer holds its own reference per the contract. `ptr<T>` passing is separate from that and a real move.

## Precondition — validate and commit the user's working state first

Before starting the compiler changes of this plan, the user's ongoing rework is completed, validated and committed by them: ptr<T> move, yaml_doc release, signature pre-pass as well as interface lists and maps. Record the base commit, compiler/runtime hashes and gate results; after that update source anchors/lines and red reproductions against exactly this state. Do not start competing changes to this still open rework.

The base rework also includes the fn gap named in the review: missing movePtrArg and missing consumed handling in the indirect fn path. With the new callee drop a named ptr<T> owner could otherwise be freed twice. The user takes over this correction before their commit; it is not an additional implementation task of this plan. Its regressions stay mandatory at the later acceptance. The separate RC consumed contract is dealt with explicitly below.

Before the first compiler fix, also record the compile time and binary size of a 9rooms build as a basis for comparison (measurement protocol in step 6).

## Current state: take the already existing fix into account

`ast_compiler.cpp:3867` now contains `registerForwardInfo`; calls are at method forward declarations around 4164 and free functions around 4603. So do not implement the original fix again independently.

Two concrete consistency points remain to be checked/corrected:

- The method pre-registration currently passes `ptr<Class>` as the implicit this; `visitTopLevel` around 4721, by contrast, inserts `ref<Class>`. this must be the same non-owning receiver in both phases.
- `registerForwardInfo` skips existing FunctionInfo entries, and the forward declaration loops skip existing LLVM functions. An already existing LLVM declaration or an incomplete FunctionInfo must not prevent signature/ownership data from being added. That is a defensive consistency check, not a further measured main finding: visitFunction later overwrites the data completely; relevant is the time window before its body emission.

The five later minimal probes remain separate acceptance criteria; complete forward metadata alone does not fix them.

## Step 0 — record reproductions and status after the base commit

Files: new descriptively named files under `tests/lifetime/`, integration into `tests/run_lifetime_leak_gates.sh` (GREEN after a successful fix). Do not presuppose numeric test IDs that are free today, since work on the working tree happens in parallel.

Starting artifacts:

- `../9rooms_v2/leak_probes/review_20261002/isolated/forward_method_1000/probe.aiml` and the green counter-order `self_chained_1000`.
- `../9rooms_v2/leak_probes/review_20261002_more/{chain,fnptr,throw,iface_consumer,fnptr_consumer}_1000/probe.aiml`, each with a matching `_local` control.

Run all six patterns including controls in a shared test run and report them individually. Reduce the forward probe to a pure compiler probe without game imports, keeping the red/green order under identical types; if no _local control exists yet, add it. The counter-order does not replace its own local control case. Record the current baseline against a fixed compiler/runtime state. Production databases are not needed. Do not enter failing probes as green gates before the associated fix actually passes.

## Step 1 — complete signature registration before body codegen

Concrete places:

- `src/core/ast_compiler.cpp:3867`: `registerForwardInfo`.
- `src/core/ast_compiler.cpp:4120`: method forward declarations in `registerTypeDecl`.
- `src/core/ast_compiler.cpp:4562`: free FunctionDecl pre-registration.
- `src/core/ast_compiler.cpp:4721`: implicit this in `visitTopLevel`.
- `src/core/ast_compiler.cpp:5449`: FunctionInfo construction in `visitFunction`.
- `src/core/context.h:91`: `FunctionInfo`.

Change: pull the signature construction into a shared helper. Set the return type, return ownership, argument names/types/lifetimes as well as this=ref<Class> completely. Reuse the LLVM function where needed. Keep already assigned fptable_index, version, source/import data; do not confuse signature completion with the body definition or the assignment of a second function slot. No body codegen during this pre-pass.

Acceptance: identical behaviour for both method orders, for free forward calls, imported/inherited and generated classgroup methods. No dependence on short method names in global producer sets.

## Step 2 — one typed call signature resolution

Concrete places:

- `src/core/ast_compiler_stmt.cpp:374`: `ownedContainerCallType`.
- `src/core/ast_compiler_stmt.cpp:384`: `resolveCallTarget`.
- `src/core/ast_compiler_stmt.cpp:413`: `ownedModelCallType`.
- `src/core/ast_compiler_call.cpp:75`: `parseFnType`.
- `src/core/ast_compiler_call.cpp:146–187`: resolution of deep receivers for the actual code generation.
- `src/core/context.h:568`: `InterfaceMethod`, next to it `InterfaceInfo`.
- `src/core/ast_compiler.h:89–95`: previous ownership/call helpers; add new declarations here.

Proposal: introduce `ResolvedCallSignature` as the semantic result: dispatch kind, return type, return ownership, parameter types/contracts and, if applicable, the concrete FunctionInfo target. An fn or interface call needs no invented concrete FunctionInfo; its static signature type is enough.

The resolution must cover free functions, simple and nested receivers, this/<., inheritance, interface signatures as well as fn<...> variables. Make the existing fn type parser shareable; no second divergent parser implementation.

Important: signature analysis must not evaluate receivers/arguments again. `resolvePropertyWritePtr` generates address/IR code and must therefore not be used blindly as a second analysis call. Separate type resolution from value evaluation or reuse the call target already determined.

`ownedContainerCallType`/`ownedModelCallType` then use this signature data. Keep builtin accessor semantics typed separately: no blanket assumption that every method called get/at is borrowed or every opaque pointer is a container. Spec 61 §5.4/§5.7 distinguishes element kinds; existing accessor tests must be kept.

Acceptance: `chain_1000` and `fnptr_1000` are green; methods of the same name in different classes must not influence each other. A counter probe confirms exactly one evaluation per receiver/argument.

## Step 3 — fix the four normal-path leaks first

After the typed resolution from step 2, unify the normal call completions. This manageable intermediate state may be accepted and committed separately with an explicitly documented red throw probe. It is not yet a complete §9.3 acceptance.

Concrete places in `src/core/ast_compiler_call.cpp`:

| Path | Existing place | Change in the normal path |
|---|---|---|
| Direct free function | `ctrArgTemps/modelArgTemps` around 555; releases around 786 | make the existing completion usable as a shared function |
| Direct method | `ctrArgTemps/modelArgTemps` around 3442; releases around 3578 | use the same completion; take the parameter offset for this into account |
| fn consumer | block around 734, immediate return around 778 | route through the shared completion after a successful call; keep the ptr/argument correction of the base commit |
| Interface consumer | vtable path around 850–1000, sret return around 987 and normal return around 999 | collect temporary argument owners and free them for all return forms including void/sret |

Together with step 2, `chain`, `fnptr`, `iface_consumer` and `fnptr_consumer` thereby become green. Do not merely remove the return in the fn path without keeping the result/void handling.

Secure the call result before cleanup and pass it on correctly. Aliasing of allowed container returns must stay valid: a return reference of its own must exist before the argument reference is released. Keep the existing string, model and move helpers; the same owner may have only one party responsible for cleanup.

Acceptance: four red patterns and all associated _local controls green at 10/1000 calls each, named caller containers still usable afterwards, results unchanged. Keep running the throw tests and carry them as RED with measured numbers. Do not present success at this stage as complete spec 61 leak freedom.

## Step 4 — correct RC consumed as a contract of its own

This gap is explicitly part of the scope. It is to be distinguished from the user's ptr<T> move fix.

Concrete places:

- `src/core/ast_compiler_call.cpp`: consumed evaluation around the direct function path (from 555), direct methods (from 3442), interfaces (from 916) and the fn path corrected by the base commit (from 734).
- `src/core/ast_compiler_stmt.cpp`: `consumeLocalDrop`, `consumeStringTemp`; distinguish callers by type class.
- `src/core/ast_compiler.cpp`: `visitFunction` and parameter incref/`pendingParamDrops`; treat the §8 call reference and permanent storage consistently.
- `src/core/ownership_analyzer.cpp` / `ownership_flow.h`: argument move decisions must not mark a named RC local as Moved merely because of consumed. Re-locate these places after the base commit.

Current state described in the review: the caller local loses its reference via consumeLocalDrop, the callee gets it without an incref. That can balance arithmetically, but violates §9.2 when the caller local is used further: after release by the callee its alias dangles.

Goal per §9.2: named RC values (string, list/map/buffer/cascade, shared) stay valid at the caller. The callee takes an additional reference of its own, not the one of the named caller. If it stores it permanently, this own reference is transferred into the storage location or shared correctly there; its parameter drop must match. Do not blindly add an extra incref when the parameter path already holds a call reference: establish exactly one additional callee reference in total and check every store against its existing retain/move semantics.

ptr<T> stays a real move; ref<T> stays non-owning. For RC temporaries a reference transfer is only allowed as an optimisation when really no surviving caller owner is needed. Safe base implementation: secure the callee reference and drop the caller temporary regularly. An RC consumed argument must therefore not be excluded across the board from the caller temp cleanup list.

Acceptance per affected RC class: the callee holds the value after return; the caller keeps reading/mutating its named value; the callee later deletes/replaces its reference; the caller stays valid and is freed exactly once at the end. Test both release orders, as well as temporary arguments and a throw after the takeover. For fn types use only contracts that its static signature actually carries; do not derive them from the incidental current function target or introduce new syntax as part of this fix.

## Step 5 — one typed temp frame including strings, models and unwind

Concrete places:

- `src/core/context.h:316–380`: `ScopeDrop`, `cleanupStack`, `stmtDropSnapshot`, `unwindPadCache`, the previous string-only `stmtTempStack`.
- `src/core/ast_compiler_stmt.cpp:224`: `visitStatement` and the statement snapshot.
- `src/core/ast_compiler_stmt.cpp:471`: `releaseContainerHandle` / `releaseContainerCopyTemp`; `:793`: `makeHandleDrop`.
- `src/core/ast_compiler_stmt.cpp:4605`: `emitScopeDrop`; `:4796`: `aimlUnwindDest`.
- Further integration points in the same file: `consumeStringTemp`, statement end, `visitRetStmt`, `visitIfStmt`, `visitWhileStmt` and `visitThrow` with the previous stmtTempStack loops.
- `src/core/ast_compiler_call.cpp:20`: `callOrInvoke`; shared completions from step 3 as well as creation/adoption of string and model results.
- `src/core/ast_compiler.h`: declarations of the typed temp frame helpers.

### Fixed slots instead of flow-sensitive activation management

Create a fixed slot in the function entry block per static temp site. Temp type and drop descriptor are fixed before the emission of the statement concerned (a typed pre-pass over the expression sites, without IR evaluation). The frame contains the complete set of its slots, not just those whose expression has already been compiled.

**At every dynamic statement start** set all slots of this frame to null/zero, before a partial evaluation can throw. A one-time initialisation at function entry is not enough. For repeatedly evaluated while conditions the reset happens at the start of every condition evaluation; short-circuit/branch paths must not reuse an old temp value. For nested statements reset only their own frame, never still needed outer owners. Before a renewed reset, values of the previous pass must be freed or transferred.

Container/string/box handles use null slots; model results need suitably aligned slots that are safely zero-initialised for their drop. The respective drop kind must actually handle the empty state safely; do not treat every resource type across the board like a nullable pointer. Reuse existing type classifiers and drop descriptors.

After a successful producer/interpolation result, write the owner into its slot immediately. If the creation itself throws, the slot stays empty. On the normal path free and empty it after the end of use; on a real ownership takeover by a local/return/store only empty the original owning slot. Treat RC consumed per step 4. For a move parameter only commit the hand-over once the necessary argument evaluation is complete; a throw before that must dismantle the owners still lying with the caller.

### Unwind pads and cache

A pad may drop all slots of the valid temp frame unconditionally, but null/zero-safely: temporaries not executed and already transferred have empty slots. No flow-sensitive activation set and no path-dependent SSA results in the pad. For nested frames handle the frame chain actually left in reverse creation order; do not free outer scopes that remain.

`aimlUnwindDest` keeps the statement snapshot filter for named locals. Temp frames are added in addition and are not filtered out by `if (snap && !snap->count(it->addr)) continue`. That way a still failed local initialisation stays protected, while already created temporary arguments of the same statement disappear correctly.

Pad cache key: function, unwind/catch target, existing normal scope drops, static statement/evaluation identity and the complete temp slot set including drop kinds/frame chain. Within the same frame it needs no activation state, because the null slots represent it at runtime. Typically one reusable pad per cleanup configuration, not a separate pad per temp site.

### Strings and models are part of this stage

The previous postponement of the string migration is dropped. `consume_throw("x-${i}")` and a throwing consumer of a heap-owning model temporary get their own red reproductions before this stage starts; they are mandatorily green at completion. String temps from interpolation, string returns and model temps run into the same typed frame. After migration remove their previous stmtTempStack/modelArgTemps releases or redirect them to the frame, so that no double drop arises.

Switch the shared completion helpers from step 3 over to the frame without worsening already fixed normal paths again. C++ RAII restores code generator state, but does not replace the LLVM output of runtime cleanup.

Acceptance: container, string and heap-owning model each on normal return, consumer throw, producer throw, a later throwing argument, loops and nested try/catch. `throw_1000` including _local and all new string/model controls green; no UAF/double free.

## Step 6 — acceptance and integration

Targeted regressions first, then the existing gates:

- Run one shared gate run with all six starting patterns and all _local controls; keep the forward order control in addition. 0 definitely lost, 0 indirectly lost, no Valgrind errors, correct results. Keep the 10/1000 scaling. At final acceptance none of these cases may stand merely as an expected RED.
- Cover dispatch × normal/throw × temporary/named; additional cases: a later throwing argument, a nested receiver, an imported/inherited method, methods of the same name, void/sret, a return that shares a container with the argument.
- Test ptr<T> arguments on all four call routes (free, method, interface, fn): named owner and temporary, normal return and throw; caller move state correct, no second release. Implicit this stays a borrow. The fn gap fixed beforehand by the user must stay permanently secured.
- The RC consumed tests from step 4 and the string/model unwind tests from step 5 run in the same acceptance package.
- Check list/map/buffer through representative cases; no type decision tailored exclusively to list<i32>.
- Run `bash tests/run_lifetime_leak_gates.sh` and `bash tests/run_memory_sanitizers.sh` according to the existing repo preconditions. Do not count a build error or a sanitizer that was not run as passed.
- Rebuild the original probe `poison_tick_zone` with the unchanged 9rooms expression line. The additional linear loss must disappear. Report the known constant 64-B allocation of the old test template separately or clean up the test setup; do not take it into the compiler gate as a tolerated general leak.
- After that rebuild 9rooms and check a normal match/phase change as well as the match end. Native minimal probes do not replace a WASM/browser run.

### Measure compile time and binary size

Measure the same 9rooms build before the first fix on the validated base commit, after the normal-path stage and after the temp/unwind rework. Record native server and WASM client targets, build command, flags, toolchain hashes, source state and target artifacts in the protocol. Compare only matching measurement pairs; avoid parallel builds during the measurement.

Measure a clean build and a build with a warm import cache separately, preferably three runs and the median for wall/CPU time; also record peak RSS. Use separate build/cache directories instead of deleting active foreign caches. Report the binary size of the same unstripped/stripped artifacts as well as the .text and exception/unwind sections (native) or the WASM file size (client). Additionally count the number of generated unwind pads/temp slots from IR or diagnostics.

Document absolute values and the percentage change. Investigate a significant increase for pad duplication, oversized slot frames or double signature analysis; do not accept it automatically with an unjustified new limit. Existing performance limits apply, otherwise present the result transparently for assessment. Without this measurement claim no completed performance acceptance.

## Recommended implementation units

0. The user validates and commits their existing rework including the fn ptr move fix. After that update the base/lines and record the measurement baseline.
1. Reproductions including RED/GREEN status and remaining signature consistency (steps 0–1).
2. Typed resolution and the shared normal call completion (steps 2–3): four additional normal-path patterns green, throw explicitly still RED. Separately acceptable, no claim of complete §9.3 fulfilment.
3. RC consumed co-ownership (step 4) as a checkable contract step of its own.
4. Fixed typed temp slots for containers, strings and models including unwind (step 5); switch throw and the new unwind regressions to GREEN.
5. A shared complete gate run, 9rooms acceptance and build cost comparison (step 6).

Definition of done: all six patterns and controls as well as the RC consumed, ptr dispatch and string/model unwind regressions green; identical results regardless of method order/dispatch; safe normal and exception exits; measured compile time/binary size; no required 9rooms rewrite and no deviation from spec 61.
