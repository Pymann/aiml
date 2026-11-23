# AIML fix plan: heap lifetimes and render memory

As of: 2026-09-07. Status: plan; no implementation yet.

Starting point: [memory measurement findings from 9rooms_v2](../../../9rooms_v2/doc/analysis/memory_checks/findings_2026-09-07.md).

## Goal and scope

Repeated work with a constant payload set should keep a bounded live heap after
the warmup. Returns, aliases, containers and runtime calls must stay correct in the
process. The fix must benefit the native server and the WASM client.

The mirror/disconnect fixes already worked on are kept. Changes to
9rooms match lifecycles, schema migrations and production data clean-up are
separate tasks. This plan includes no data wipe and no deployment.

## 1. Evidence and causes still open

- The report documents strong growth with constant lifecycle counters.
  That fits temporary allocations; the counters alone, however, prove neither
  complete clean-up nor a particular allocation path.
- `src/core/ast_compiler_stmt.cpp` explicitly emits `aiml_ser_deep_copy` for model value locals from `kv.get_copy`.
  These additional allocations are independent
  of the KV decode cache. Cache eviction alone cannot reclaim them.
- Probe E shows that dynamic strings must be investigated as well. Probe D
  with literals is only a control without dynamic string content.
- RA and RB both call `populate` with interpolation. Equal growth
  therefore does not prove a separate instantiate/destroy leak.
- `aiml_prefab_set_text` in `src/sys/prefab_runtime.cpp` assigns to a `std::string`.
  That manages its previous content itself. The statement in the report
  that the old text is not freed there is therefore not proven.
  The AIML input string can still leak as a temporary.
- RC contains populate and draw, but no complete frame completion with a
  buffer swap. Additional RSS can also come from driver allocations or pending
  GPU commands. A C++ renderer leak needs isolated proof.
- At review time the existing render probe is set to 300 iterations, while
  the report names 20000. Source state, binary and parameters must match.

## 2. Work package A: reproducible baselines

Before compiler changes, correct and isolate the existing probes:

1. `tests/test_mem_leak_probe.aiml`: switch probe C to a constant payload size
   (replace an existing value instead of appending per iteration). Run the probes individually in
   their own processes. Use the results so that optimisation does not remove the work.
   Use a temporary DB directory per run; no fixed
   recursively deleted paths and no production data.
2. Extend A/B/D/E with bare interpolation without a container, reused
   dynamic strings, models with exclusively scalar fields, nested
   models as well as separate decode and copy.
3. `tests/test_mem_render_probe.aiml`: measure separately populate without the renderer, `set_text` with
   literals and reused text, instantiate/destroy without populate, draw
   of an unchanged scene and complete frames with swap.
   Bound the text length and character set; font/glyph warmup before the measurement.
4. Examine native debug binaries with ASan/LSan and/or a heap profiler.
   Supplement RSS with live allocations with stacks, anonymous and file-backed
   memory pages as well as relevant object counters. In the WASM client, record the used
   allocator heap separately from the WebAssembly.Memory capacity.
5. Measure the warmup and several equally sized batches. Store compiler/runtime version,
   build flags, parameters and results together. Examine clean and
   incompatible test KVs separately.

Acceptance: for every source classified as a leak there is an allocation stack and
an explained missing release path. RSS growth alone is not enough for an
attribution to KV, `set_text` or draw.

## 3. Work package B: define the ownership contract

Before automatic freeing, reconcile the applicable rules in `spec/10_collections.md`,
`spec/15_models.md` and `spec/45_heap_serialization.md` with the implementation.
Create a shared table for compiler and runtime:

| Origin / operation | Contract to clarify or enforce |
|---|---|
| String literal | Static; never free |
| Interpolation / string-returning runtime | Owning return value or explicitly borrowed view |
| Model deep copy | The target owns the copied heap fields |
| `kv.get`, `list.get`, field reference | Borrowed view; document owner and invalidation |
| `kv.get_copy` → model local | Distinguish the cache intermediate value and the additional independent local copy |
| Assignment, parameter, `push`, `set` | Keep the existing alias contract; define copy, retain or move unambiguously |
| `set_text` | The runtime copies synchronously; treat the lifetime of the input after the call separately |
| Return / durable storage | The value outlives the producer scope through correct ownership transfer or shared ownership |
| Pointers, callbacks, FFI | Explicitly distinguish borrowed, owned and escaping |

Preferred approach: ownership metadata in codegen plus type-specific
copy/move/drop operations. Whether shared data needs retain/release
depends on the existing alias rules. Do not silently turn all assignments
into deep copies. Describe required semantics/ABI changes separately before
implementation.

Decision gate: a scope destructor is only permissible once the escape paths
of the value are clarified. No blanket `aiml_ser_free_decoded` for arbitrary
models: literal, borrowed and owned fields as well as shallow copies can otherwise
trigger a double free or use-after-free. A mere owned marker on the whole
model is not enough for mixed or later replaced fields.

Acceptance: locals, temporaries, parameters, fields, containers, returns and
runtime boundaries are covered. Unclarified cases are not freed automatically in an unsafe way;
a targeted compiler diagnostic may be required.

## 4. Work package C: implement lifetime codegen step by step

Affected entry points: `src/core/context.h`, `ast_compiler_stmt.cpp`,
`ast_compiler_expr.cpp`, `ast_compiler_call.cpp`,
`src/core/ast/control_flow_builder.*`, `src/core/ast/function_builder.*`,
`list_impl.*`, `builtin_registry.cpp` and the string/serialisation runtime.

1. Carry ownership and initialisation state on symbols and expression results.
   AST shape or LLVM pointer type alone do not distinguish ownership.
2. Keep temporaries alive until the end of their full expression.
   Free them after synchronously copying calls; transfer them or share them correctly on store/return.
   Cover nested calls and several arguments.
3. Emit type-specific drops for owned strings, lists, maps, buffers and recursive
   model fields. Exclude literals and borrowed pointers. Align the ownership of
   decode results with the regular runtime containers.
4. Clean-up in reverse initialisation order at all scope exits:
   normal block exit, every loop iteration, `break`, `continue`, all
   returns and supported exception/unwind paths. Handle partially initialised and
   already moved values safely.
5. Secure return values before clean-up: direct and multi-level model returns,
   returning a field as well as container returns. The callee must not destroy
   transferred ownership again.
6. Handle overwriting: evaluate the RHS safely first, free the previous ownership
   correctly, take over the new one. Consider self-assignment and references to old
   fields. `push/set/remove/clear` and container growth must
   fulfil the same ownership contract as locals.
7. Integrate explicit `free` so that no second automatic free follows.
   Detect escaping stack addresses. Do not treat pointers, interfaces and class instances
   as owning models merely because of their representation.

Acceptance (binding): the isolated native probes must pass under ASan/LSan:
no UAF, invalid/double free and no lost own
allocations. N/2N measurements of the live heap supplement this check, but do not
replace it. Falling or flat RSS alone does not count as acceptance.
Returned and stored values stay valid after the producer scope;
equivalent WASM probes pass. Unavailable sanitizers mean an
open acceptance, not a silent substitution by pure RSS measurement.

## 5. Work package D: repair confirmed renderer leaks separately

This package can be implemented after A independently of the larger compiler package,
provided the probes prove a runtime leak:

- In `src/sys/prefab_runtime.cpp`, `ui_runtime.cpp` and the called font/GL paths,
  follow the measured allocation stacks. Free temporary layout data via RAII;
  do not bypass existing string/vector destructors.
- Separate durable text/glyph/texture caches from lost allocations.
  If changing contents accumulate without bound, define limits and invalidation.
  Record GPU resources separately.
- Destroy must clean up the instance, children and registrations. A destroy
  deferred during draw completes only at the safe end of the frame. Shared
  fonts and textures must not be deleted per instance.
- No `free(text)` in `set_text`: the input can be a literal or a
  borrowed address. An AIML temporary visible there belongs to package C.

Acceptance: an unchanged scene with a complete frame cycle, text changes and
instantiate/destroy show no linear growth of live resources after warmup.
Draw without swap remains a diagnostic control.

## 6. Verification and integration

Targeted regressions for:

- literals and dynamic strings; nested models/lists/maps; empty/null;
  copy/move/self-assignment; overwriting; `push` with subsequent use of the
  original; `remove/clear`; borrowed `get`; copy → mutation → KV write-back.
- direct and multi-level returns, several return branches, `break/continue`,
  exceptions and partial initialisation, explicit free as well as runtime calls with
  temporary and stored arguments.
- memory probes A/B/C/D/E and isolated renderer probes with a constant set.
  After warmup compare batches with N and 2N: no own live heap growing proportionally to N.
  Heap/WASM capacity and RSS do not have to fall after free.

After the targeted checks pass, run the relevant existing compiler, collections,
serialisation, mirror and prefab tests. Before completing the
compiler package, run the full suite; document existing failures separately.
Do not repeat the complete suite after every small step.

Rebuild the native runtime and the WASM runtime as well as both 9rooms binaries accordingly.
Then an isolated 9rooms measurement with a fresh test DB: menu, exactly one match,
match end and tab close. Use the same activity as in the baseline;
a comparison run must cover at least the roughly 50 minutes observed before.
Evaluate memory per tick/poll/frame and the lifecycle counters together.

## 7. Order, delivery and possible mitigation

Urgency and decision gates before the big compiler package:

1. **A and decode validity first:** isolate the baselines and carry the error contract
   from 8.6.2 through runtime and scheduler. Invalid blobs must not be processed as
   successfully loaded matches. This is a mandatory
   precondition for the server re-measurement run.
2. **Server re-measurement as a gate before C:** the get_copy_at mitigation already used in the scheduler
   is implemented, but its effect against the server OOM is still
   unconfirmed. After the decode gate is met, measure at least the previous approx. 50 minutes
   with a constant match set: RSS, live heap and lifecycle counters.
   Check clean test data and controlled incompatible test blobs separately.
3. **Prioritise by the measurement result:** with a flat curve the acute
   OOM risk for the measured load profile is defused; C becomes careful
   quality work without OOM time pressure. With further growth, first isolate the remaining
   allocation stacks. Do not pull C forward as an unconfirmed emergency rebuild.
4. **B, then C:** ownership contract and compatibility check before activating
   automatic drops. C is a fundamental expansion of the lifetime codegen,
   not a small local fix. Existing language semantics are preserved; only if
   a change is demonstrably unavoidable, clarify the concrete case with the
   user beforehand. Regression tests and longer memory runs are authorised.
5. **D and integration:** confirmed renderer fixes can be pulled forward separately after A.
   Then native/WASM integration and the before/after measurement.

Every part gets matching evidence and an updated specification. Existing
uncommitted user changes, in particular in `mem_runtime.cpp` and
`mirror_runtime.cpp`, are preserved. Reverting individual changes must be possible without
resetting other people's work.

`get_copy_at` is already in use as a server mitigation in the scheduler:
the target buffer must free its previous heap and rule out escaped aliases.
Merely reusing the struct shell does not solve the problem.
A blanket switch of the scheduler to `kv.get` is likewise not a safe fix because of KV invalidation.

The plan neither assumes that all observed renderer growth is a real leak,
nor does it promise that a single model destructor fixes all client
and server leaks. This delimitation is checked by the baselines.

## 8. Concrete changes with line numbers

Line state: checked on 2026-09-07 while adding this. On changes, use the named
function names as search anchors. Helper names in the proposals are drafts,
not existing APIs. All proposals concern a later implementation.

### 8.1 Correct the probes — package A

| File / line | Proposal |
|---|---|
| `tests/test_mem_leak_probe.aiml:56`, `probe_set` | Replace appending `"more"` with replacing an existing scalar element, e.g. `idx: [mrec.nums, 0, ccnt]`. The payload stays constant. |
| `tests/test_mem_leak_probe.aiml:139` | Parameterise probe selection and iterations; start probes separately, use the results and compare N/2N after warmup. |
| `tests/test_mem_leak_probe.aiml:114` | Add interpolation without a list and a list with dynamic strings created outside the loop as controls. |
| `tests/test_mem_render_probe.aiml:26`, `populate` | Add variants with literals and reused text; separate interpolation from text storage. |
| `tests/test_mem_render_probe.aiml:75`, `probe_frame` | Measure draw-only without populate as well as a complete frame with the existing swap/frame-end API separately. |
| `tests/test_mem_render_probe.aiml:103`, `:115` | Parameterise iterations; destroy `held` before window/GL teardown. Font/scene teardown per the existing API contract. |

### 8.2 String refcounting and producers — B/C

- `src/sys/string_runtime.cpp:37`, `:74`, `:81`: `aiml_str_alloc/incref/decref`
  already exist. Build on them. Incref/decref currently read a
  magic header in front of arbitrary passed addresses; the address size check proves
  no valid memory in front of it. Before broader use, provide a safe managed origin
  in codegen and, for mixed runtime boundaries, a safe registry/handle
  contract. Secure the registry for concurrent use. Do not probe arbitrary
  literals by reading in front of their address.
- `src/sys/prefab_runtime.cpp:6021`, `aiml_prefab_text_value`: returns `strdup`.
  After a consumer audit, switch to `aiml_str_dup` and mark the result as an owned
  managed string. Check the dropdown string returns from `:3839` too.
  An existing `free(char*)` would be invalid afterwards and must be switched as well.
- `src/core/ast_compiler_expr.cpp:43–68` and `src/core/string_utils.h:13`:
  concat creates new accumulators. Release the replaced owned accumulator after a successful
  concat, pass the final result on as an owned temporary. Capture number-to-string
  segments; do not free borrowed segments and literals.
- `src/core/ast_compiler_expr.cpp:1152–1164`: reading a string list element
  already performs an incref. Capture the expression as owned and release it later;
  no second blanket retain on `decl`. Keep the existing
  `tests/test_mirror_close_string_uaf.aiml` as a control.

### 8.3 Ownership metadata and scope clean-up — B/C

Extend `src/core/context.h:27` (`Symbol`) with ownership/initialisation information;
`:186` (`Scope`) with an ordered clean-up list. The variable map stores
no initialisation order. Draft for shared compiler helpers:

```cpp
enum class Ownership { Static, Borrowed, Owned, Shared };
registerCleanup(slot, type, initializedFlag);
emitDrop(slot, type);
emitCleanupsUntil(scopeBoundary);
transferOrRetainResult(source, destination);
```

Carry initialized/moved/maybe-initialized as data flow states on expressions too.
Conditional initialisation and moves may need runtime flags;
a single C++ bool at compile time is not enough. Take `is_byref_arg` into account.
Mixed field ownership needs a uniform field contract or metadata;
an owned marker only on the whole model is not enough.

- `src/core/ast_compiler_stmt.cpp:1285`, `visitBlockStmt`: currently saves/restores
  the symbol table. Enter/leave clean-up frames; emit normal clean-up only
  in basic blocks that are not yet terminated.
- `:1362–1367`: free body locals before the while jump back, not only after the
  whole loop. Set initialisation/move flags correctly per iteration.
- `:1190–1236`: secure the return result, clean up the scopes being left, then
  `ret`. Capture void/implicit returns too; append nothing after terminators.
- `:1374`, `visitTry`: clean up the scopes actually left in the
  exception/unwind path too. Free only initialised resources. Attach supported
  break/continue emitters to the same scope boundaries.
- Do not change only the builder facades: these statement visitors emit control flow
  themselves and must actually use the new helpers.

### 8.4 Deep copy, replace and return — C

| File / line | Intervention after the ownership contract is clarified |
|---|---|
| `src/core/ast_compiler_stmt.cpp:586–607` | After a successful local deep copy, mark ownership/initialisation and register clean-up; do not free the KV cache slot along with it. |
| `src/core/ast_compiler_stmt.cpp:862`, `:1104` | Check further deep copy assignment paths for occupied targets and use a shared replace helper. |
| `src/core/ast_compiler_stmt.cpp:1219–1236` | Keep the existing return deep copy, transfer the result to the caller and exclude it from the callee clean-up. |
| `src/sys/ser_runtime.cpp:696`, `aiml_ser_deep_copy` | Keep the precondition "target is empty"; add a checked replace variant for occupied targets. |
| `src/sys/ser_runtime.cpp:471`, `fre` | Use only for compatibly owned decode fields; after a list free also zero size/capacity. A general model drop needs a field contract. |

Draft for replace, also for self-assignment and source aliases:

```cpp
tmp = zeroInitialized<T>();
if (!tryDeepCopy(tmp, source)) {
    dropPartiallyInitialized(tmp);
    propagateError();             // the previous target is preserved
}
drop(destination);
move(destination, tmp);           // tmp afterwards without ownership of its own
```

The current deep copy returns void: a checked variant or a corresponding error contract
is needed. Do not free the target first, because the source can point to its fields.

### 8.5 Container elements — C

`src/core/list_impl.cpp:56` (`push`), `:200` (`clear`), `:287` (`remove`):

- Give push/insert the AIML element type or a lifetime descriptor; copy/retain/
  move per contract. The LLVM pointer type alone does not distinguish string ownership.
- Clear currently only sets size=0. Drop owned elements beforehand; the buffer capacity
  may stay. Otherwise the elements are no longer reachable for a later free.
- Remove drops exactly the removed owned element, shifts the remaining slots
  as an ownership transfer and neutralises the last unused slot. Do not additionally release
  shifted elements.
- Container drop: elements first, then the buffer. Do not destroy borrowed parameters.
  Check maps accordingly. Shallow model copies from at/get must not become two
  owners of the same heap.

### 8.6 Secure the existing get_copy_at

Already present: `src/sys/db_runtime.cpp:1661` and
`src/core/ast_compiler_call.cpp:1832`. The mitigation in section 7 is by now
an audit of this existing path, not a request to reimplement it.

1. Correct the comment from `db_runtime.cpp:1653`: "zero per-call allocation" is
   wrong; decode allocates heap fields anew. What is reused is the struct shell.
2. **Binding decode gate before the server live test:** missing key = 0 is
   already present (db_runtime.cpp:1668/1672). In the found typed path, however,
   the decode status is ignored and return 1 is executed afterwards.
   Clarification on the review: in the current source state aiml_ser_decode
   already returns int, not void (ser_runtime.h:84, ser_runtime.cpp:713): 0 on success,
   -1 on error. What is missing is passing on and handling this status.
   Proposal for get_copy_at: 1 = copied successfully, 0 = key missing,
   -1 = invalid blob; reconcile the API contract in the spec and codegen.
   Scheduler/wrappers may only continue on 1. On an error,
   do not read, tick or write back a partially decoded target value;
   clean up partial allocations safely and define the target state explicitly.
   Check schema mismatch and structurally corrupt blobs separately. Recreate the three
   reported old match keys as a test case; mark invalid keys for the
   current data state and exclude them from the regular tick, with a
   defined recheck after a data change. No production data wipe.
3. Require the target to be an initialised buffer of its own, not a KV cache alias.
   Make the codegen check at `ast_compiler_call.cpp:1832` more precise; diagnose unknown aliases
   or rule them out via a safe API contract. Also drop the last content
   at the end of the scope.
4. If the old target content should be preserved on a decode error, use the replace from 8.4.
   Currently it is deleted before decode; specify the behaviour.

### 8.7 Renderer — concrete check points, conditional changes

- `src/sys/prefab_runtime.cpp:3662`, set_text: keep the std::string assignment.
  Capture the call contract "borrowed input, synchronously copied" so that owned
  input temporaries are freed correctly afterwards. No free(text).
- `:4807`, draw_scene: measure stacks for layout, render lists and GL separately.
  Local vectors have RAII. Reusable scratch vectors would be an
  allocation optimisation, not a blanket leak fix; mind reentrancy.
- Pending delete sweep from `:4875`: check complete deregistration with instance/child counters.
  Only add missing frees if a remainder is proven;
  keep the safe end-of-frame point in time.

### 8.8 Acceptance and activation order

Strings: balanced retain/release and valid aliases after container/mirror
close. Models: A/B plus return/loop/exception controls. Containers: E plus
push/clear/remove with permitted alias uses. get_copy_at: missing key,
incompatible blob, repeated refill and the final drop. Renderer: isolated
complete frames. ASan/LSan are the binding acceptance for C; equivalent
WASM probes and live heap measurements are added. RSS alone is not enough.

Start with 8.1 and the contract from B. Activate model drops only together with the necessary
transfer/container rules. These lines are not independent
one-line fixes.
