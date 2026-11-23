# Lifetime: remaining work, risks and concrete implementation

As of: 2026-09-09. Basis: the user's decision on chapter 61 and a renewed direct reading of the source. WP-C is discarded. This plan reintroduces neither its whole-program escape analysis nor a garbage collector.

Status: a detailed work plan, no implementation and no runtime acceptance report. No git was used. Line numbers are snapshots of a working tree edited in parallel; function names are the stable anchors. Re-read the current state before every change.

## 1. Goal and binding limits

Heap-carrying values should be freed deterministically without creating new use-after-free, double-free or data loss errors. Authoritative is `spec/61_lifetime_ownership.md`: locally determinable ownership rules, explicit function contracts, consumption that is unambiguous at compile time and no hidden runtime drop flags.

A lower RSS slope alone is not an acceptance. A probe must actually complete its work successfully and stay clean under a suitable memory checker. A run that aborts early or is miscompiled must not count as a fixed leak.

This plan presents concrete proposals for implementation. Proposals that still need a specification decision are marked as such explicitly. It activates no further drops and does not change the meaning of `persistent` on its own authority.

## 2. Current state and correction of earlier statements

| Area | Observation when this document was created | Consequence |
|---|---|---|
| Drop switch | `src/core/context.h:317`: `lifetimeDropsEnabled = true` | The existing cleanup paths are active. Incomplete ownership checking is therefore runtime-relevant. |
| Direct string temporaries to consumed parameters | `ast_compiler_call.cpp:420–429` and `:2473–2483` now also remove the evaluated temporary via `consumeStringTemp`. | Do not carry the earlier finding as open unchanged. Verify direct interpolation and both call paths specifically. |
| Qualifier syntax | Chapter 61 now describes the modifier at position 4 after the initial value. | The earlier spec/parser contradiction is corrected. |
| Harness | The native probe is run separately and checked for errors; Valgrind uses its own error code. | A substantial improvement. Completeness of the instrumented run remains an acceptance criterion of its own. |
| Map drop | `ast_compiler_stmt.cpp:380`: registration still explicitly deferred; runtime and emission already exist. | Existing primitives are no proof of safe activation. |
| WAL startup error | Inaccessible WALs were observed as a concrete server startup blocker; the error behaviour was changed afterwards. | Proves neither that all login problems are solved nor that lifetime drops are safe overall. |

The statement "missing ownership precision always only causes leaks" is too strong. Merely omitting a drop can cause a leak. Together with shallow copies, active target drops and accesses that are still allowed, however, that can turn into a UAF.

## 3. Priorities and order

1. Secure the string temporary corrections already made and the reliability of the harness.
2. Implement ownership transfer on assignments unambiguously; secure local copy cases and self-assignment.
3. Track ownership state per control flow path and reject ambiguous merges. Use-after-move belongs in this package.
4. Give return and other scope-leaving exits path-correct cleanup.
5. Extend box drops together with sufficient borrow/function contracts.
6. Only activate map drops after a completed layout and mutation audit.
7. Check every package in isolation, after that validate the relevant 9rooms flows against a server.

The work requires control flow analysis within a function. It does not automatically require a whole-program escape analysis or a complete rebuild of the compiler onto SSA. LLVM SSA for values and the ownership state of a source variable are different things.

## 4. P1b: assignment and branch ownership

### 4.1 Relevant code places

- `src/core/ast_compiler_stmt.cpp:826`, `visitSetStmt`: early call of `consumeLocalDrop` for the RHS.
- `src/core/ast_compiler_stmt.cpp:1419`, `consumeLocalDrop`: removes matching entries from the cleanup frames.
- `src/core/ast_compiler_stmt.cpp:1512`, `visitIfStmt`: branch construction.
- `src/core/ast_compiler_stmt.cpp:1557`, `visitWhileStmt`: loops and the back edge.
- `src/core/context.h:275–299`: `ScopeDrop`, `cleanupStack`, `pendingParamDrops`.
- Further transfer sites: list push, map put, member/index set, return as well as free and direct method calls in `ast_compiler_call.cpp`.

### 4.2 Concrete dangerous case

A schematic flow, not an already run repro:

```text
outer scope:
    x = independent model with heap fields
    inner scope:
        y = empty local model
        y = x
    use x
```

If `y = x` only copies the heap pointers, `x` is removed from its cleanup and `y` is freed at the end of the inner scope, `x` afterwards points to freed memory. Chapter 61 requires an independent copy for a local assignment into a target area that lives equally long or shorter.

An additional simple edge case is `set: [x, x]`: it must neither lose the only cleanup entry nor free its own heap prematurely.

### 4.3 Proposal for the ownership representation

Represent ownership and a pending release separately. Membership in `cleanupStack` alone is not sufficient proof of ownership.

Per relevant variable keep a stable identifier and at least the following information:

- origin: owned, borrowed, static or not yet reliably classified.
- declaration scope and known owner binding.
- qualifier: scope, persistent or consumed.
- path state: available, moved or not yet initialised, as far as this distinction is needed.
- drop kind and metadata for model, string, list, map or box.

The concrete C++ names are implementation details. What matters is that shadowing does not merge two variables of the same name and that a deleted drop entry does not serve as a substitute for an explicit moved state.

### 4.4 Classify the assignment first, then change cleanup

`visitSetStmt` must not remove the source drop before the target, the RHS type and the kind of transfer are fixed.

Proposed flow:

1. Resolve the target and check writability.
2. Evaluate the RHS. Keep the previous target value valid meanwhile.
3. Determine copy or move according to the binding spec table.
4. Take self-assignment and alias relations into account.
5. On copy, produce an independent temporary copy before the old target heap is freed. "Evaluate the RHS first" alone is not enough for a pointer into the old target heap.
6. Only on successful preparation free the old target value and take over the new one.
7. Only on an actual move mark the source as moved and suppress its later release on this path.

If deep copy can fail, this flow needs an evaluable success contract. `aiml_ser_deep_copy` returning `void` is not enough for a reliably error-proof replace. Keep the existing decoder/KV work separate, but take this interface into account.

`persistent` is not a general licence to share sources. If the spec requires a copy, an independent copy must actually come about, or the operation must be rejected for non-copyable resources.

### 4.5 Branch states and merge

Before a branch, save the ownership state as the entry state. Analyse THEN and ELSE each from the same entry state. After that merge only the predecessors that actually continue at the merge.

| State at continuing predecessors | Result |
|---|---|
| available everywhere | available; a later drop stays |
| moved everywhere | moved; no later source drop |
| partly available, partly moved | compile error per §6 |
| predecessor ends with return/throw | not a normal merge predecessor; its own exit cleanup there |

A return branch must not delete the cleanup of the sibling path that does not return from the shared compiler data structure.

Check loops additionally: a value created before the loop must not be moved in the first iteration and consumed again in the second. Zero iterations, the back edge, break and re-initialisation need defined state transitions. For the first safe implementation step, conservatively reject unclear patterns instead of determining the state only by the textual order of the statements.

### 4.6 Acceptance

- A local copy stays independently usable after the end of the source scope and of the target scope respectively.
- Self-assignment keeps the content and exactly one owner.
- A real member/container move outlives the end of the source scope.
- Conditional consumption with an ambiguous merge is rejected.
- A terminating branch does not change the ownership state of its continuing sibling.
- Test loops: zero, one and several iterations; repeated consumption of an outer source is detected.
- Valid cases show no invalid reads, double frees or proportionally growing lost allocations.

## 5. Use-after-move and warning-first

Use-after-move is a safety precondition of active ownership transfer, not just a convenience diagnostic. After release by the new owner, a renewed access to the source is potentially a UAF.

### 5.1 Check points needed

- Variable resolution and reading in `ast_compiler_expr.cpp`, as well as a method call on a variable.
- Passing as a function argument, member/index access and renewed consumption.
- Re-initialisation: may make a previously moved slot valid again without freeing its old heap again.
- Borrow aliases remain a separate problem; merely nulling the source slot does not secure heap pointers that escaped before.

### 5.2 Rollout proposal

Warning-first is justifiable in an analysis mode in which the affected new destructive operations are not yet active or are replaced by provably safe substitute semantics. A warning with move and drop still active unchanged does not make the program safe.

If a program is to keep compiling despite ambiguous ownership, a safe, documented handling must exist for exactly this case. Do not claim "only a leak" across the board. A deliberately deferred drop can serve as a bounded transitional measure, but does not automatically secure other active owner drops.

## 6. P2a: cleanup before return and other exits

### 6.1 Relevant places

- `src/core/ast_compiler_stmt.cpp:1287`, `visitRetStmt`.
- `src/core/ast_compiler_stmt.cpp:1439`, `visitBlockStmt` and normal drop emission.
- `src/core/ast_compiler.cpp:4586–4596`: staged string drops for consumed parameters.
- Try/catch processing in the same statement compiler.

### 6.2 Concrete rework

Move the emission of a drop into a shared helper, for example `emitDrop(entry)`. Next to it provide a helper for the scopes actually being left on a particular exit. The emission reads the path-specific state; it does not globally delete the states of other paths.

For return:

1. Evaluate the return expression including conversions and existing return decorators.
2. Secure the final return value or its independent ownership.
3. Exempt transferred sources/temporaries from release on this path.
4. Free all other active owners in reverse initialisation order: inner scopes first, then outer ones.
5. Only after that emit the return terminator.
6. If `ret_dead` is kept for further code generation, do not generate again any live cleanup that is logically still pending there. Track reachability explicitly.

A return value that is a field of or a borrow onto a local owner to be dropped needs a copy or an allowed transfer beforehand. Otherwise closing the return leak turns a pointer that used to survive into a UAF.

Normal block exit, return and break must not trigger the same release twice on the same executed path. Break only leaves the scopes up to the loop target. Record supported error/unwind exits separately; a return correction alone is not a complete exit acceptance.

### 6.3 Acceptance

- Explicit final return and an early return from nested blocks.
- A consumed string that is not passed on is freed exactly once.
- A returned owner stays valid at the caller.
- Returning a model field is correctly secured before the owner drop.
- No double drop via the normal exit and return.
- The return conversion/decoration must not access old data after the release.

## 7. Box drop and borrow contracts

A box drop is more than freeing the outer allocation. Nested owned fields must be cleaned up beforehand; borrowed or static fields must not be treated as owned in the process.

### 7.1 Scope without WP-C

What is required is locally checkable origin and explicit function contracts. A complete whole-program borrow checker is not a precondition. Cases that cannot be proven locally must be rejected conservatively or left out of the extension.

Clarify before activation:

- The `{new:T}` origin stays known across hand-overs; a borrowed `ptr<T>` does not automatically become a box.
- Consumed parameters transfer ownership only according to the signature contract.
- Storing/returning a borrow: copy, diagnostic or an explicitly allowed lifetime binding per the binding spec.
- Temporary boxes, member boxes and repeated replacement each have a defined release point.
- Resource/handle types use their matching destructor; not every handle may be treated with an ordinary `free` or cloned by copying the pointee.

### 7.2 9rooms migration and acceptance

Record the affected places specifically, do not blindly rewrite all pointers: services with stored references, callbacks, mirror/network objects as well as match/player lifetimes. Carry out the migration based on compiler diagnostics and concrete owner chains.

Server check: start, login, token validation, deck read/save, create/end a match, connection loss and reconnection. Every flow needs demonstrated functional success as well as a matching memory check. Check file permissions and available KVs beforehand as the operational basis; an unavailable database is not a lifetime test.

## 8. Complete map drop

### 8.1 Relevant places

- `src/core/map_impl.cpp` and `map_impl.h`: layout, hashing, insert/replace/delete, growth and rehash.
- `src/sys/string_runtime.cpp:132` at the time of reading: `aiml_map_free`; the line can move with parallel runtime extensions.
- `src/core/ast_compiler_stmt.cpp:380`: deferred registration.
- `src/core/ast_compiler_stmt.cpp:1488–1498`: drop emission for map entries.
- `src/core/ast_compiler_call.cpp`, map put path: ownership of arguments and temporary strings.

### 8.2 Layout audit

The runtime release must interpret the same layout as the map operations. With open addressing, the number of valid entries is not equal to the index of the last occupied slot. Typically, the capacity slots must be walked and their occupancy states evaluated; take the concrete state values from the implementation.

Align the following invariants in writing:

- Offsets, integer widths, key/value stride and alignment.
- Distinguish a free slot, an occupied slot and a tombstone unambiguously.
- No release of uninitialised or already deleted slots.
- Rehash transfers existing ownership; old slot shells must not free the same elements again.
- The container allocation and the element heap are separate resources.

### 8.3 Mutation audit

For `put`, define explicitly whether key and value are copied or consumed. Replacing an existing key needs a rule for the old value, the existing key and the newly supplied key. Temporary arguments must neither end up in the statement cleanup after takeover nor be lost on a rejected insert.

Delete/clear/replace and the final drop must fit together. A correct final drop does not cure a leak on repeated overwriting. A correct put does not cure a double free on rehash.

Check strings, nested models, lists and maps separately. Raw/borrow pointers must not be freed as owned elements merely because of their pointer representation.

### 8.4 Acceptance before activation

- Empty map, one element, many elements and deliberately created collisions.
- Growth/rehash, tombstones, delete followed by re-insert, repeated clear.
- Repeated replace of the same key with a newly allocated value.
- Keys and values as strings as well as supported nested heap types.
- Put with direct interpolation and with local sources.
- Acceptance under a memory checker plus an N/2N allocation comparison; no UAF/double free, no proportional loss at a constant working set.

## 9. String temporary correction: do not re-implement, but secure

The free and direct method paths read most recently call both `consumeLocalDrop` and `consumeStringTemp` for consumed parameters. That addresses the reported case in which the callee and the statement cleanup freed the same string.

Verification must use direct interpolation; a string stored beforehand via `decl` only checks the local path:

```text
call drop_str("tmp-${idx}")
call holder.keep("kept-${idx}")
```

Check both variants: the receiver frees the string itself, or stores it and reads it after the caller has returned. Also check borrow parameters as the counter-case: no ownership transfer, the statement temporary is only freed after the synchronous call.

Do not confuse the new handling with the still open general moved state. Removing a temporary cleanup does not replace a use-after-move diagnostic for named variables.

## 10. A reliable acceptance harness

Existing basis: `tests/run_memory_sanitizers.sh`, probes under `tests/_mem_sanitize/`.

Separating native correctness from instrumented memory checking makes sense, because RSS values under Valgrind must not be interpreted like native RSS values. That does not imply a general permission to ignore the instrumented probe exit, however.

Proposal:

1. The native run must succeed and execute its functional assertions.
2. For the Valgrind mode, explicitly disable only the native RSS bound; keep evaluating the functional assertions as binding.
3. Require a complete Valgrind report and a regular end. A missing log, missing end, signal or tool start error are not a success.
4. Check the Valgrind error code and the report content; do not classify an arbitrary non-zero exit as a harmless RSS deviation.
5. Run a deliberately faulty control probe separately and recognise its expected error.
6. Compare leak figures at a fixed amount of work. A leak value printed without assessment is not yet a leak acceptance.
7. Report unavailability of the checking tool as an acceptance not run; a skip must not count as a passed safety check in a binding gate.

Every relevant probe should record the binary/source state and the exact call parameters in its result. An old binary cannot validate a new source change. No git commands are needed for that; build time and, if needed, file hashes are enough for attribution.

## 11. Decision gates and filing of results

| Gate | Required proof | Not yet derivable from it |
|---|---|---|
| Temporary hand-over | direct and local string arguments correct, both call paths clean | general ownership safety |
| Assignment/data flow | copy/move table, branches and loops respected | safe box/map release |
| Return cleanup | return valid, other owners freed exactly once | complete unwind coverage without its own check |
| Box/map | origin, layout and all mutations secured | freedom of the 9rooms migration from problems |
| 9rooms e2e | functionally successful flows and valid memory runs | all conceivable applications checked |

File results each as a separate dated report with the cases checked, failures, limits and measurement data. Do not close open points merely because of green partial probes.

## 12. Practical next implementation step

After verifying the already changed temporary hand-over, first correct `visitSetStmt` and the ownership state. The new decision about copy/move must come before the mutation of the cleanup list. After that build branch merges and use-after-move on the same state model. Only then put return cleanup on top of this model and activate additional owner kinds.

This flow keeps the packages small, stays with the explicit approach from chapter 61 and avoids a supposed leak fix silently freeing foreign heap.
