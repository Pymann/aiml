# Lifetime — implementing the difficult packages (progress + imperfections)

As of: 2026-09-09. Basis: `doc/plans/lifetime_remaining_work_verbose_2026-09-09.md`.
Autonomous implementation at the user's request ("really implement everything … if this or that does not become
perfect, record it. but definitely push it through.").

Adopted decisions (delegated by the user):
- **Q1 branch merge / use-after-move:** warning-first (non-breaking). Switch to a hard
  error later, deliberately.
- **Q2 `aiml_ser_deep_copy` contract:** treated as infallible for now (returns `void`).
  Free-old-target + a fallible copy contract are noted as an imperfection (below).
- **Q3 sequencing:** package by package, each validated by build + harness + aiml suite.

Master gate for EVERYTHING: **`lifetimeDropsEnabled = true`**; the aiml suite + valgrind do NOT cover the
9rooms server paths (KV/login/deckset/match). Server e2e remains the final proof.

---

## Package status

### P1b — assignment ownership (§4)
- **Increment 1 (done/being validated):** local→local heap model assignment = **deep copy**
  (the source keeps its drop); **self-assignment (`set:[x,x]`) = no-op**. Member/container/
  return sinks stay a move (`consumeLocalDrop`). Fixes the §4.2 UAF.
  - File: `ast_compiler_stmt.cpp` visitSetStmt.
  - Probe: `tests/_mem_sanitize/assign_local_copy.aiml`.
- **Imperfection (deliberate, §4.4.5/§4.4.6):** deep_copy overwrites the target WITHOUT freeing its old
  heap → **reassigning an already occupied local model leaks the old heap**
  (under-drop, safe). A fresh decl local (the §4.2 case) has no old heap. Free-old-target
  + a success contract come with the full assignment data flow.
- **Open:** local→local for **string/list/map** (currently: string/list local→local = leak,
  not UAF → safe; copy semantics follow), the copy/move determinant table complete (§7.0).

### P2a — return/exit cleanup (§6) — ATTEMPTED, REVERTED (blocked on the §4 data flow)
- Implemented (helper `emitScopeDrop` + return cleanup before the terminator, path-correct,
  no double emit) and enabled for scalar/void/plain local returns.
- **Regression: 2 new double frees** (`test_list_list_inner_values` abort 134,
  `test_class_member_shared_tensor` segv 139). **Cause = the central finding below.**
- Reverted; the return leak remains (under-drop, safe). `emitScopeDrop` stays (refactor).

### Box drop (§7) — DEFERRED (borrow escape as an error + migration + server e2e needed)
### Map drop (§8) — DEFERRED (occ/grow-rehash audit; the put move UAF fix is in = a safe leak)
### use-after-move (§5) — DEFERRED (needs the §4 data flow; as a warning otherwise a flood of noise)

---

## ⚠️ CENTRAL FINDING (affects the WHOLE remaining roadmap)

The scope drops (list/map/model) assume that a value local **owns** its heap.
But a value local that is copied **shallowly** from the inner container of a model
(`decl:[cb,list<i32>]; set:[cb, ability.inner_list.get(0)]`, or a view from `get()`)
**shares** the buffer with the real owner (the model). On dropping, **both** free the same
buffer → **double free**.

Currently this is **masked**, because functions using this pattern usually end with `return`
→ the terminator skips the block drops → the locals are NEVER dropped (leak,
safe). P2a unmasks it. The same danger exists latently for **void functions** with this
pattern (the aiml suite hits none → 363/1 green, but 9rooms could).

**Consequence:** P2a, box drop, full map drop and use-after-move ALL first need the full
§4 ownership data flow (classification of every local: owned / borrowed / shared view / moved,
§4.3), which distinguishes exactly these shallowly shared views from real owners. Without it
every more aggressive drop unmasks these double frees. **That is the reason why the plan sequences
P2a/box/map AFTER the data flow — confirmed by the P2a regression.**

**That also means drops ON as a whole is only trustworthy after server e2e** (the suite masks
the pattern). `lifetimeDropsEnabled=true` stays as found; server e2e is a mandatory gate.

---

---

## Final state of this autonomous session

**Safely implemented + validated (aiml suite 363/1 = baseline, sanitizer harness all green):**
- P1b inc1: local→local model assignment = deep copy; self-assignment = no-op.
- (earlier packages) consumed qualifier (free fn + direct + interface methods, with a match
  check), const param, list<string>/list<Model> scope drops (E leak 7.68MB→0),
  borrow escape warning, the P1a direct interp double free fix, the map.put UAF→safe leak fix,
  a hardened harness (native+valgrind separately, --trace-children, ERROR SUMMARY required).

**Blocked on the §4 ownership data flow (owned vs. borrowed/shared view), NOT implementable
without new double frees:**
- P2a return cleanup (attempted → 2 double frees → reverted; see the central finding).
- Box drop (additionally: borrow escape as an error + 9rooms migration + server e2e).
- Full map drop (additionally: occ/grow-rehash audit).
- use-after-move (additionally: warning noise without data flow).

**The honest next step** therefore remains the plan's §4 core: implement the intra-function
ownership state (owned/borrowed/shared view/moved). It is the shared
precondition of ALL remaining packages AND only makes the already active drops watertight against the
shared view pattern. Before that: server e2e with drops ON (mandatory gate).

---

## §4 ownership data flow — IMPLEMENTED (core, 2026-09-10)

Implemented per `ownership_dataflow_implementation_packet_2026-09-10.md`:
- **Increment A** (`src/core/ownership_flow.h/.cpp`): a pure data model (OwnFact/FlowState/
  Origin/LifeBits/OwnershipPlan), `joinFact`/`joinStates`/`sameState`, `mayDrop`. LLVM-free.
- **Increment B** (`src/core/ownership_analyzer.h/.cpp`): a conservative analyzer. Two passes
  (IDs first → the loop fixed point terminates). A slot is `Owned` ONLY if it is declared exactly like the
  drop registration (fresh list/model/string/get_copy) AND is never rebound via `set`;
  every `set` on such a slot → `Unknown` (shared view protection).
- **Increment D (basic)**: `if` join + `while` fixed point in the analyzer (no longer textual-global);
  non-convergence → `suppressFunctionDrops` (conservative).
- **Increment E**: `visitFunction` builds the plan (RAII), `visitDeclStmt` tags every ScopeDrop
  with its `ownershipId`, `visitBlockStmt` only emits a drop if `mayDrop` releases it at the
  block exit as provably owned. No plan / unknown block → old behaviour.

**Validation:** aiml suite **402/363/1** (baseline, the analyzer crash-free on EVERY function),
sanitizer harness all green (owned drops keep firing → E leak fix kept), and the new
probe `ownership_inner_view_void.aiml` proves: the shared view double free is now ALSO **prevented**
on a normal (void) block exit (VG_RC=0). That fulfils the core acceptance criterion of the
package (§11: normal scope drops rest on proven ownership; branches/loops no longer
textual; unknown resources conservative).

**Deliberately conservative / open in the package:**
- **C (plan-driven lowering)**: NOT rebuilt — E gates the drops without switching assignment
  lowering to `Transfer`. The C acceptance (model copy independent; local list
  views not freed) is functionally reached by E + P1b inc1; the full copy/move
  table (§7.0) stays open.
- **F (interprocedural retain effects, shared tensor)**: NOT modelled — safe in the current scope
  thanks to the conservative default (a member store of a borrow param creates no new
  drop). Needed for P2a reactivation.
- **P2a return cleanup**: BUILT (mayDrop-gated + return value owner exemption §6.2), but
  **DISABLED** (via `(void)` casts in visitRetStmt). Result of the reactivation:
  - `test_list_list_inner_values` → **now green** (intra-function shared VIEW suppressed by `mayDrop`). ✓
  - `test_class_member_shared_tensor` → **still segfaults** ✗. Cause, **precisely**: not the
    tensor destructor — a minimal **path-only repro** (`shared_path.aiml`, a leaf with
    path+list<string>, copied shallowly into two holders) reproduces the same
    `aiml_ser_free_decoded` double free. Root = **shallow value model member store sharing**:
    `bind(a_leaf: Leaf)` takes the model by value (shallow copy), `set:[<.leaf, a_leaf]` stores
    it shallowly → several members share the same inner heap → recursive `free_decoded` frees it
    several times. The intra-function `mayDrop` does not see this **internal** aggregate sharing.
  - **Blocker/root fix = increment C**: `set:[<.member, valueModel]` must **deep copy**
    (spec §3/§7.0 "value type = owned, copy on store"), so that members are independent. That fixes
    P2a AND the **pre-existing latent** internal aggregate double free (masked today, because
    such functions return). C is a **broad semantics decision** (member store becomes a
    copy instead of a move; perf: a deep copy per store) → to be put to the user.

---

---

## Increment C — member store copy/move (plan-driven, 2026-09-10)

User decision: "copy borrowed, move owned". Implemented:
- **Analyzer** (`ownership_analyzer.cpp`): param facts (Borrowed) in the entry state; for every
  MEMBER store `plan.statements[set].transfer` is set — an `Owned` local source → `Move`,
  otherwise (param/view/rebound) → `DeepCopy`.
- **Codegen** (`visitSetStmt`): `memberDeepCopy` read from the plan; on DeepCopy the
  consumeLocalDrop of the sink is skipped (the source keeps ownership), and the model member store
  gets a fast path that takes the source ADDRESS via `emitLValueAddress` and emits
  `aiml_ser_deep_copy` into the member (also works for a value model param loaded as a struct value
  — the pure `srcPtr isPointerTy` path missed it).

**Validation:** new probe `ownership_shared_member_void.aiml` (value model stored by value in two
holders, the aggregate dropped at a NORMAL exit) → **VG_RC=0** (before: double free in
`aiml_ser_free_decoded`). `store_then_drop` (owned local move) still green. (The full suite/harness
gate was running at the time of this note.)

**Important intermediate note (session restart):** a scratchpad repro was lost on the restart
→ a `YAML::BadFile` abort (exit 134) briefly faked an error; probes therefore belong
persistently in `tests/_mem_sanitize/`, not in the scratchpad.

**P2a REACTIVATED & green (2026-09-10):** with C + the §4 gate, P2a now passes BOTH tests that were
broken before (list_list_inner_values via mayDrop; class_member_shared_tensor via C).
Return cleanup in `visitRetStmt` active (void + alias-safe return shapes), every drop gated by
`mayDrop` against the `earlyExits` facts + return value owner exemption (§6.2).
**Full gate: suite 402/363/1 (baseline), sanitizer harness all green** (incl. the new probes
`ownership_shared_member_void`, `return_cleanup`). `return_cleanup` proves: the leak on return
for owned list<string> locals is fixed (0 `aiml_str_alloc` leaks over 500 calls).

**With that A, B, C, D(basic), E AND P2a are pushed through and validated.** What remains open are only those
named in the package as SEPARATE follow-up packages: F (explicit interprocedural retain effects — currently
covered conservatively), box drop, full map drop (occ/grow audit) — plus server e2e.

## 2026-09-10 — 9rooms reality check: feature set to DEFAULT OFF (hardening phase)

The aiml gate was green (suite 402/363/1, sanitizer harness all green), BUT the **9rooms suite
crashes with the feature ON in ~20 % of the tests** — the synthetic probes did not cover the real
aliasing patterns. Two classes:
- **Double frees (rc=134/139):** `aiml_ser_free_decoded` twice. Confirmed via valgrind
  (46 errors, `Invalid free`) + a minimal repro. **Root cause #1 (FIXED):** the member store of a
  `map/list/buffer` **field** from a borrowed source (`set: [dat.produces, req.produces]` in
  `card_service.persist_from_req`) does a **shallow memcpy** (ast_compiler_stmt.cpp ~1261) →
  the target field shares the buffer with the source; since BOTH owners now get a scope drop →
  double release. Fix: increment C only covered value *model* sources (deep_copy), NOT
  container fields. Now: on `memberDeepCopy` (non-owned source) the **drop of the
  target base local is revoked** (it is not a sole owner) → crash → bounded leak. There are
  FURTHER patterns (e.g. the `test_activate_ability` init), not yet rooted individually.
- **Logic regressions (rc=1/3/6):** the increment C deep_copy changes aliasing semantics (a member
  that used to alias is now an independent copy → mutations do not propagate).

**Decision (user, 2026-09-10): feature DEFAULT OFF, then harden.**
- `lifetimeDropsEnabled` is now **default false** (context.h). `Compiler::compile()` reads
  `AIML_LIFETIME_DROPS` (unset ⇒ OFF; `=1` ⇒ ON; `0/off/false` ⇒ OFF). The switch gates EVERYTHING:
  drop emission (visitBlockStmt/visitRetStmt), the increment C `memberDeepCopy` **and** the
  local→local model deep_copy (§4.2) → OFF = complete pre-feature behaviour.
- `tests/run_memory_sanitizers.sh` exports `AIML_LIFETIME_DROPS=1` → keeps exercising the feature.
- **Re-enable gate:** the FULL 9rooms suite must be green with drops ON before the default goes back ON.
- Verified: with `AIML_LIFETIME_DROPS=0` tests that crashed before run again (card_all_types_kv,
  create_spell, card_base_engine_api, effect_processor, mana_choice). `test_activate_ability`
  no longer crashes, but has its OWN latent shallow aliasing logic error
  (`bob life=30` instead of 20) — which the deep_copy feature would even FIX; to be investigated separately.

**Open hardening TODOs for the re-enable:**
- Container field **deep_copy** (instead of the drop revocation) as a zero-leak root fix: synthesise a per-field `AimlTypeNode`
  + `aiml_ser_deep_copy` on the one field (respect the map key/val kind).
- Root the remaining double free patterns (activate_ability init etc.) one by one.
- Check the logic regressions from the increment C semantics (which 9rooms places relied on alias
  mutation — possibly these are real bugs that deep_copy correctly uncovers).

## 2026-09-10 (continued) — container field deep_copy = zero-leak root fix

The drop revocation workaround was replaced by the **real root fix**: a new helper
`Compiler::emitContainerFieldDeepCopy` (ast_compiler.cpp) builds the `AimlTypeNode` for the container field via
`serBuildSubtree(typeName,0)`, emits a LinkOnceODR global and calls
`aiml_ser_deep_copy(dst,src,nodes,n)` — i.e. `dat.produces = req.produces` (a map/list/buffer field
from a borrowed source) now gets an **independent heap**. That way the target model
keeps its scope drop → **no double free AND no leak** (valgrind: 0 unsafe, 0 runtime leak; all
"definitely lost" are the LLVM/JIT infrastructure baseline). Probe gate green incl. the new
`member_container_deepcopy` probe.

**Effect on 9rooms (with `AIML_LIFETIME_DROPS=1`):** the whole `persist_from_req`/`create_bundle`
crash class is gone — `test_card_all_types_kv`, `card_base_engine_api`, `create_spell` green, and
`test_activate_ability` no longer crashes (134→rc=1, that is the SEPARATE dice_roll logic error,
see below). Full 9rooms suite run with the feature ON: **no more crashes** (only rc=1/3/6
logic fails).

**Foreign cause identified (NOT our feature): non-deterministic soulmage damage.**
`sources/match/dice_roll.aiml` (UNTRACKED/new in 9rooms) makes soulmage damage a range
`floor(value/2)..value` (rulebook §7), deterministic only if `mat.dice_queue` is seeded.
Tests like `effect_processor`/`creature_damage`/`activate_ability` do not seed → flaky. Purely on the
9rooms side (have the tests seed, like `test_combat_dice`).

**Still open for the re-enable:** check whether the feature-ON-only logic fails (cost_processor,
mana_choice, mana_pick, amorphous_mana, forum_service) are increment C deep_copy semantics
regressions (alias mutation that deep_copy now decouples) or likewise dice/pre-existing.

## 2026-09-10 (continued 2) — map.put DECLARED key move gap fixed (mana_choice regression)

An isolated ON-vs-OFF categorisation of the suite fails showed: almost all "fails" of the feature-ON suite
were **contention flakes** (under parallel valgrind load), NOT real regressions. In
isolation: most pass ON & OFF; `effect_processor`/`activate_ability` fail BOTH
(the dice_roll feature, on the 9rooms side); `enchantment_picker` fails BOTH (pre-existing). The ONLY
real feature-ON regression was `test_mana_choice` (deterministically ON=fail, OFF=pass).

**Root cause (fixed):** `MapImpl::put` stores key/value RAW (move, no dup/incref). A **declared**
string local passed as a key (`decl colkey "${color}"` → `m.put(colkey, v)`) has
a kind-1 decref scope drop in `cleanupStack`. `map.put` only called `consumeStringTemp` (only
inline stmt temps) — the declared local kept its drop → its decref at scope exit freed
the string the map still holds → a corrupt/empty map. Fix (ast_compiler_call.cpp ~1477):
additionally `consumeLocalDrop(call->args[0/1])` (like list.push). Minimal repro + new probe
`map_put_declared_key`. `test_mana_choice` now green with the feature ON; memsan gate green.

**Re-enable status:** with the feature ON (a) all crashes are gone (container deep_copy) and (b) the
only real logic regression (mana_choice) is fixed. Remaining 9rooms fails = its own
untracked dice_roll feature (have the tests seed) + pre-existing (enchantment_picker). A full
isolated ON suite run is running for confirmation. After that, default ON again can be considered.

## 2026-09-10 (continued 3) — re-enable readiness: compiler side clean

Definitive isolation measurement (ON vs OFF, without suite contention):
- **Zero NEW crashes caused by the feature.** Every 134/139 seen with the feature ON EITHER
  also occurs with the feature OFF (pre-existing) OR was a contention artefact of the parallel
  valgrind/suite/user measurement.
- `test_enchantment_picker`: intermittent **segv (139) + logic error "second effect lost"
  ALSO with AIML_LIFETIME_DROPS=0** (4/6 runs crash with OFF) → **pre-existing 9rooms bug**, not
  our feature. To be investigated separately on the 9rooms side.
- `effect_processor`/`creature_damage`/`activate_ability`: fail ON & OFF = the dice_roll feature
  (the tests do not seed `mat.dice_queue`) — on the 9rooms side.
- `mana_choice`: the only real feature-ON regression, FIXED (map.put declared key move).
- Remaining suite "fails" (forum_service, mana_pick, aura_ops, card_states, chat_nickname, …) pass
  in isolation ON & OFF → **contention flakes** (the suite JIT-compiles the whole
  9rooms sources per test; under parallel load race/timeout).

**Conclusion:** the spec/61 feature side is hardened against real 9rooms aliasing patterns (container
deep_copy + map.put key move). No more known feature-caused crash or logic regression.
**The default stays OFF for now** (user gate: the full 9rooms suite must be green with the feature ON — that
currently fails ONLY on 9rooms' own things: dice_roll test seeding + the pre-existing
enchantment_picker segv). For the final gate proof a quiet (contention-free)
full ON suite run is needed.

## 2026-09-11 — WASM client crash → class "model shared shallowly across several containers"

Client WASM crash with the feature ON: `RuntimeError: index out of bounds` in the Asyncify rewind
(doRewind/handleSleep) = heap corruption. Narrowed down headless (WASM runner `tools/run_wasm_test.sh`,
`AIML_LIFETIME_DROPS=1`):
- The core feature paths are **clean** in WASM: `member_container_deepcopy`, `map_put_declared_key`,
  `list_model_drop`, as well as async+drop via `time.sleep_ms` (Asyncify suspend) — all green.
- Single container deep_copy of NESTED model fields (`list<Model>`/`map<string:Model>`) individually:
  green.
- **Reproduced (native 134 AND WASM):** the same model in TWO containers of one owner
  (`src.items.push(inr)` + `src.bykey.put("k", inr)`). push/put move the model SHALLOWLY → both
  containers share `inr`'s inner heap; the owner's model drop (`aiml_ser_free_decoded`) runs
  over both containers → **double/triple free**. OFF = green (only a leak). NOT the container
  deep_copy fix — `copyover` is not involved at all.

**Root:** the drop logic assumes that every owned model owns its heap ALONE. `push`/`put`
of a model into a container, however, is a SHALLOW move; a model reachable from >1 container
(or used further as a live local after the insert) is freed several times on drop.
Single-container cases work (that is why the probes + `card_all_types_kv` were green). That is the
probable cause of the client crash (cards/models in several views/zones).

**Fix needed (large, design):** DEEP-copy models on `push`/`put` into containers (every
container element owns an independent heap — analogous to increment C for member stores), OR model container
element ownership differently. Broad + perf costs → deliberately NOT implemented ad hoc.
**The default stays OFF.** Validate WASM additionally once the class is fixed.

## 2026-09-11 (continued) — list<Model>.push deep copy (model container aliasing fixed)

Before implementation, 9rooms usage was surveyed (user question "does 9rooms assume the same instance?"):
- 164 `get_copy→push` (value copy), `push({at:[l,i]})` (list→list copy), get-modify-**set**
  write-back. Shared identity ALWAYS via `list<ptr<Model>>`. **0 occurrences** of "push a local, then
  mutate `.field`". ⇒ 9rooms treats value containers as values; **no `push_at`/`push_from`
  needed** — the type (`list<Model>` vs `list<ptr<Model>>`) already encodes the intent.

**Fix:** `list<Model>.push` of a value model now copies the element DEEPLY, unless the source is
a provably dead, fresh owned local (a live kind-0 scope drop) → then a move (cheap, as
before). The increment C principle applied to push. Implementation: `ListImpl::pushDeepCopyModel`
(list_impl.cpp: append an empty slot + `aiml_ser_deep_copy` into it) + call site routing
(ast_compiler_call.cpp push handler): source address = element pointer (get/at) directly, otherwise
spill the struct value into a temp alloca. Only value models (serNodes); ptr<>/scalar/string untouched.
Everything behind `lifetimeDropsEnabled` (OFF = unchanged).

**Verified (feature ON):** new probe `model_into_two_containers` (the same model in 2 lists +
`at`→push): native PASS, valgrind 0 errors + 0 runtime leak, **WASM PASS**. Move fast path intact
(`card_all_types_kv`/`card_base_engine_api`/`card_pick` green with ON). Memsan gate green (+m2c).

**Still open:** `map<K:Model>` VALUE insert does NOT deep-copy yet (MapImpl put has 2 store sites,
update+insert). **9rooms uses no `map<*:Model>`** → not client-relevant, but a language gap:
a model as a map VALUE + drops ON can still double free (only synthetic repros). To follow up as a
small package of its own (analogous to `pushDeepCopyModel`). After that: rebuild the WASM client with the feature ON
and test it live.

## Running imperfection/deferral list
1. deep_copy assumed "infallible"; no free-old-target on reassignment (P1b inc1).
2. Map drop registration still deferred until the occ/slot audit (the map.put UAF is already fixed as a
   safe leak via consumeStringTemp).
3. use-after-move a warning at first, not an error.
4. Server e2e pending for ALL packages.
