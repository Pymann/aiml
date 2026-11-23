# Implementation packet: §4 ownership data flow

As of: 2026-09-10. Basis: `lifetime_hard_packages_progress_2026-09-09.md`, chapter 61 and direct reading of the current compiler. Git was not used. This document contains proposed C++ changes; it is not an already applied patch and not a passed test run.

## 1. Assignment and scope

First implement an intra-procedural ownership pre-pass and bind its result to the already existing normal scope drops. Do not switch on return clean-up, box drop or full map drop again at the same time. The reverted return attempt stays reverted until this package is accepted.

Adopted decisions from the progress report remain visible:

- Q1: warning-first. A diagnostic alone is not a safety measure; conservative handling of unsafe new drops is a mandatory part of it.
- Q2: the existing `void` deep copy contract stays unchanged for this package. Do not add free-old-target before success and alias safety have been clarified separately.
- Q3: small steps, each with a build and matching checks; no sign-off merely by passing the previous suite.

WP-C stays rejected. What is needed is a finite, function-local data flow with known call contracts, no whole-program escape analysis and no runtime drop flags.

## 2. Concrete initial bug

`tests/test_list_list_inner_values.aiml` contains:

```aiml
- decl: [var_cb, list<i32>]
- set: [var_cb, { call: [var_ab.activation_cost.get, 0] }]
```

`var_cb` receives the list shell of a foreign buffer contained in the model. Its type stays `list<i32>`, but it does not own the buffer because of that. The list free registered at `decl` no longer matches the value in the slot after `set`. At the same time, the model drop of `var_ab` can free the same buffer recursively.

`tests/test_class_member_shared_tensor.aiml` additionally contains `set: [<.inner, an_inner]` from a model parameter. That is a different problem: stored shallow sharing across a function boundary. A purely local variable status in the callee is not enough for it. An exposed retain/escape effect must reach the caller; otherwise the new recursive drop must not count as safe. Do not blanket-clone or free tensor handles.

The reported exit codes 134/139 come from the progress report. They were not reproduced again while writing this plan.

## 3. Code map

Lines are anchors of the state read, not stable patch offsets.

| File / place | Change |
|---|---|
| `src/core/context.h:47`, `Symbol` | add a stable `ownershipId`, do not use qualifiers as proof of ownership |
| `src/core/context.h:279`, `ScopeDrop` | add an owner ID; clean-up stays a drop recipe, not a state database |
| `src/core/context.h:292–299` | keep the active ownership plan next to the clean-up/parameter frames |
| `src/core/ast_compiler.h:64` | replace/migrate `consumeLocalDrop`; declare the new plan interfaces |
| `src/core/ast_compiler.cpp:4354`, `visitFunction` | build the analysis in after signature resolution and before the body IR; save/restore the context |
| `src/core/ast_compiler_stmt.cpp:314–377` | bind decl drop candidates to the analysed declaration ID |
| `:813–945`, `visitSetStmt` | remove the default move at `:910`; apply the copy/move/view plan per statement |
| `:1473`, `consumeLocalDrop` | no more deleting from all frames; the ownership plan determines the sink transfer |
| `:1495`, `emitScopeDrop` | keep the primitive; the caller checks the right to free at the concrete exit |
| `:1527–1554`, `visitBlockStmt` | derive drop permissibility from block exit facts instead of the decl type |
| `:1556`, `visitIfStmt`; `:1601`, `visitWhileStmt` | data flow in the pre-pass; do not discover branches only while emitting |
| `src/core/ast_compiler_call.cpp` | provide resolved call/container contracts, argument transfer and retain effects |
| `src/core/ast_compiler_expr.cpp:15ff.` | check expression origin against the pre-pass during lowering |
| new `src/core/ownership_flow.h/.cpp` | pure state/merge/analysis logic, independent of LLVM emission |

`exodus.json:821–825` already contains `src/**/*.cpp`. Before changing the build configuration, check whether the new files are picked up automatically by that.

## 4. Increment A: data model and pure merge functions

### 4.1 New file `src/core/ownership_flow.h`

The following C++ core is meant as a concrete template. Further adapters/visitors are described explicitly in the following sections; this excerpt alone does not yet implement a complete analyzer.

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace AIML {
class Expr;
class Stmt;
class DeclStmt;
class BlockStmt;

using OwnershipId = std::uint32_t;
using ResourceId = std::uint32_t;
constexpr OwnershipId NoOwnership = 0;

enum class Origin : std::uint8_t {
    None, Owned, Borrowed, SharedView, Static, Unknown
};
enum LifeBits : std::uint8_t {
    Uninit = 1, Live = 2, Moved = 4
};
struct OwnFact {
    Origin origin = Origin::Unknown;
    std::uint8_t life = Uninit;
    std::set<ResourceId> resources;
};
struct FlowState {
    bool reachable = true;
    std::map<OwnershipId, OwnFact> slots;
};
enum class Transfer : std::uint8_t {
    None, SelfAssign, DeepCopy, Move, BindView, LegacyUnknown
};
struct StatementPlan {
    Transfer transfer = Transfer::None;
    OwnershipId source = NoOwnership;
    OwnershipId target = NoOwnership;
    OwnFact result;
};
struct OwnershipPlan {
    std::map<const DeclStmt*, OwnershipId> declarations;
    std::map<const Stmt*, StatementPlan> statements;
    std::map<const Expr*, OwnFact> expressions;
    std::map<const BlockStmt*, FlowState> normalExits;
    std::map<const Stmt*, FlowState> earlyExits;
    std::set<ResourceId> suppressNewDrops;
    // Unresolved alias/retain effects: no new drops for this function.
    bool suppressFunctionDrops = false;
};

OwnFact joinFact(const OwnFact& a, const OwnFact& b);
FlowState joinStates(const FlowState& a, const FlowState& b);
bool sameState(const FlowState& a, const FlowState& b);
}
```

`SharedView` here means legacy sharing without a free claim of its own, NOT reference counting and NOT `shared<T>` as a new language feature. Owner qualification and path liveness are orthogonal: a single enum `owned/borrowed/moved` would lose information at merges.

### 4.2 New file `ownership_flow.cpp`: executable merge core

```cpp
#include "ownership_flow.h"

namespace AIML {
OwnFact joinFact(const OwnFact& a, const OwnFact& b) {
    OwnFact out;
    out.origin = a.origin == b.origin ? a.origin : Origin::Unknown;
    out.life = static_cast<std::uint8_t>(a.life | b.life);
    out.resources = a.resources;
    out.resources.insert(b.resources.begin(), b.resources.end());
    return out;
}

FlowState joinStates(const FlowState& a, const FlowState& b) {
    if (!a.reachable) return b;
    if (!b.reachable) return a;
    FlowState out = a;
    for (const auto& [id, rhs] : b.slots) {
        auto it = out.slots.find(id);
        if (it == out.slots.end())
            out.slots.emplace(id, joinFact(OwnFact{}, rhs));
        else
            it->second = joinFact(it->second, rhs);
    }
    for (const auto& [id, lhs] : a.slots) {
        if (b.slots.find(id) == b.slots.end())
            out.slots[id] = joinFact(lhs, OwnFact{});
    }
    return out;
}

bool sameState(const FlowState& a, const FlowState& b) {
    if (a.reachable != b.reachable || a.slots.size() != b.slots.size())
        return false;
    for (const auto& [id, x] : a.slots) {
        auto it = b.slots.find(id);
        if (it == b.slots.end()) return false;
        const OwnFact& y = it->second;
        if (x.origin != y.origin || x.life != y.life ||
            x.resources != y.resources) return false;
    }
    return true;
}
}
```

Remove block-local symbols from the state before block merges. Missing outer states are deliberately treated conservatively above; this case should be rare once lexical scopes are built correctly.

### 4.3 Stable identities and resources

Assign IDs once before the fixed-point analysis: one `OwnershipId` per declaration and parameter, one `ResourceId` per allocation/copy site. Never create new IDs in the loop run, otherwise the analysis does not terminate. Shadowing gets different IDs; names are exclusively for lexical resolution and diagnostics.

For a model drop it must be known which resources it frees recursively. A list as a model field therefore belongs to the resource set of the model owner. When binding a view, pass this resource along. When a foreign field is built in, the transitive resource set of the target model is updated too.

First conservative representation: a resource group per model/container including its heap fields. Different dynamic indices may coincide at first. That creates additional suppressions/leaks, but must not miss an alias. Do not simply give every field access an independent new allocation ID.

## 5. Increment B: origin of all relevant expressions

### 5.1 Analyzer adapter matching the existing AST

In the new module, implement an `OwnershipAnalyzer` with `analyzeBlock`, `analyzeStatement`, `classifyExpr` and a lexical symbol environment. Actual AST fields:

- `VariableExpr::name`;
- `CallExpr::functionName`, `args`;
- `AtExpr::container`, `indices`;
- `NewExpr::typeName`, `initValues`;
- `StringInterpolationExpr::parts`;
- `SetStmt::target`, `value`;
- `IfStmt::condition`, `thenBranch`, `elseBranch`;
- `WhileStmt::condition`, `body`;
- `BlockStmt::statements`, `RetStmt::value`.

`classifyExpr` receives resolved type/call metadata. No classification based on the text `.get` alone: tensor get, list get and KV get have different returns. Extract the existing resolution logic into a shared helper instead of inventing a second, diverging dispatch in the analyzer. For not yet resolved overloads/imports, conservatively return `Unknown`.

### 5.2 Binding origin table

| Expression | Fact / lowering contract |
|---|---|
| Scalar | `None`, no heap drop |
| String literal | `Static`, never a header decref merely because of the type `string` |
| Fresh container | `Owned`, allocation site resource |
| Variable | fact of the slot, no new resource |
| Model field / container view | `Borrowed` or `SharedView`, resources of the real owner |
| `at` / resolved list get with a heap value | view; a shallow struct shell does not make it an owner |
| `kv.get` | borrow of an external cache owner, not scope-owned |
| `kv.get_copy` as a runtime pointer | likewise cache-bound; only the actually created local deep copy is owned |
| `kv.get_copy_at` | status value; the target content becomes owned on success, empty on error per the runtime contract; missing keeps the previous state |
| `new` | known box origin, box drop nevertheless off in this package |
| Interpolation | fresh only on an actual allocation; a single-string pass-through stays view/static |
| Ordinary parameter | borrow, also for a value model or a by-ref list |
| consumed parameter | owned according to the existing signature contract; root and drop kind separate |
| Unknown call return / external handle | `Unknown`; do not claim a generic recursive free |

For `get_copy_at`, either evaluate its return value per path or merge the state alternatives. If the target was borrowed before and missing is possible, the result is not simply Owned.

## 6. Increment C: assignments without a blanket move

### 6.1 Rebuild `visitSetStmt`

Remove the fallback `if (setStmt->value) consumeLocalDrop(...)` at `:910`. Read the transfer from `OwnershipPlan::statements.at(setStmt)`. Install the result state only after actually successful lowering; a diagnostic/error path must not record a move.

Concrete structure as an insertion template; helper names are adapters to be implemented anew:

```cpp
const StatementPlan& op = ctx.ownershipPlan->statements.at(setStmt);
if (op.transfer == Transfer::SelfAssign) return;

// Keep the existing type, const and void checks BEFORE any mutation.
IRValue* rhs = visitExpression(setStmt->value.get());
if (!rhs) return;

switch (op.transfer) {
case Transfer::DeepCopy:
    // Dispatch to supported copy primitives; evaluate the RHS once.
    emitPlannedCopy(setStmt, rhs, op);
    break;
case Transfer::Move:
    emitPlannedMove(setStmt, rhs, op);
    break;
case Transfer::BindView:
case Transfer::LegacyUnknown:
    emitLegacyAssignment(setStmt, rhs);
    break;
case Transfer::None:
    emitLegacyAssignment(setStmt, rhs);
    break;
case Transfer::SelfAssign:
    break;
}
```

`emitLegacyAssignment` arises by extracting the previous lower assignment part. It must not evaluate the RHS again. `emitPlannedMove` removes matching string temporaries exactly like the already corrected call sinks; it does not delete global clean-up recipes. The right to free follows the exit plan.

### 6.2 Transfer rules of the first package

- Self-assignment stays a no-op after the required target/type check.
- A supported model local→local copy stays a deep copy. Do not free the previous target heap yet, as before; document the leak explicitly.
- Shallow binding of an inner container is marked as `BindView`. The new slot must not free its buffer. A possibly previously created empty shell is a different allocation; record its loss as an open replace leak.
- Mark string/list/map local→local as an independent owned result only if the lowering really copies independently. Until the respective copy primitive exists: `SharedView`/legacy, with an alias relation and a conservative drop prohibition.
- A member/container sink is not a valid move merely because of a dot in the name. Ordinary parameters and views cannot transfer ownership. Do not "clone" non-copyable resources via the serializer.
- Reinitialising a moved slot sets it to live with a new origin. Do not free old resources again.

The goal of this package is a correct picture of actual ownership. A complete switch of every legacy operation to the new copy semantics is a separate increment and must be proven per type.

## 7. Increment D: branches, loops and warning-first

### 7.1 Analysis before IR, not retroactively

Analyse the complete function before its body IR is emitted. Otherwise a drop in the THEN block is already emitted before an alias is discovered in the ELSE block. Deleting a clean-up entry later does not repair this IR.

`analyzeIf` as a C++ template:

```cpp
FlowState OwnershipAnalyzer::analyzeIf(const IfStmt& node, FlowState in) {
    analyzeCondition(*node.condition, in); // calls in the condition can have effects
    FlowState left = node.thenBranch
        ? analyzeBlock(*node.thenBranch, in) : in;
    FlowState right = node.elseBranch
        ? analyzeBlock(*node.elseBranch, in) : in;
    return joinStates(left, right);
}
```

The condition is taken into account conservatively on both edges in the analysis; branch-precise refinements optionally later. A return stores its exit state and sets `reachable=false`. No merge with `ret_dead` as a seemingly live predecessor.

### 7.2 Loop fixed point

```cpp
FlowState head = entry;
for (;;) {
    FlowState bodyIn = head;
    analyzeCondition(*loop.condition, bodyIn);
    FlowState back = analyzeBlock(*loop.body, bodyIn);
    FlowState next = joinStates(entry, back);
    if (sameState(next, head)) break;
    head = std::move(next);
}
```

This is the core, not the complete exit handling: collect break exits and feed them to the loop exit; continue/back edge to the header; return/throw not to the back edge. The condition can also change resources on leaving, so form the exit from the stable header after the condition analysis. Unknown constructs must not count as effect-free.

Emit diagnostics only after convergence or deduplicate them per AST location. Merge plan facts per location on repeated analysis, do not just keep the last iteration. On an implementation error/iteration limit, abort the analysis conservatively and suppress new function drops; do not free the random last state.

### 7.3 Secure warning-first technically

`Live|Moved`, unknown ownership or an alias used after a move produce a warning and mark the affected resource group in `suppressNewDrops`. That must cover ALL newly introduced frees of this group, including recursive model/container drops and temporary drops.

If the group cannot be tracked reliably across a function boundary, a warning with a locally suppressed source drop is NOT enough: a callee may already free. Do not lower such a consumed/escape case destructively in the new ownership mode. A conservative legacy mode is only permissible if its previous operations stay unchanged; otherwise a targeted rejection of the not implemented transfer. This is not a blanket switch of the use-after-move diagnostic to an error, but a boundary of the safe new lowering.

This transition mode avoids additional unproven drops. It does not prove that arbitrary already unsafe legacy code becomes safe. In particular, explicit `free`, unknown retain callbacks and mutating external functions need their own contract.

## 8. Function contracts: necessary for the shared tensor pattern

Besides parameter qualifiers, add minimal effects to `FunctionInfo`: result fresh/borrowed/unknown; borrowed result dependent on argument N; argument retained/stored; argument mutated or consumed. No method-name heuristic as a substitute.

For `Body.setup(an_inner, ...)`, the store into `<.inner` must yield a retain/share effect for `an_inner`. At the caller, this resource must thereby also show up in the new target model or be conservatively exempted from recursive drops. A pure callee warning is not enough.

The first stage may treat unresolved, recursive or external calls as `UnknownRetain`. Lock all reachable heap resources of their arguments for new drops; with unknown reach, use the function-wide fallback. No complete interprocedural fixed-point inference required: explicit or locally derivable signature effects and conservative defaults suffice. Known pure borrow calls need a proven contract so that the fallback does not devalue every function.

## 9. Increment E: codegen exclusively from exit facts

### 9.1 `context.h`

Add `#include "ownership_flow.h"`. Add at the END of both `Symbol` and `ScopeDrop` (at the end, so that existing aggregate initialisations are not shifted):

```cpp
OwnershipId ownershipId = NoOwnership;
```

In the `CompilerContext`:

```cpp
const OwnershipPlan* ownershipPlan = nullptr; // borrowed plan from visitFunction
```

For parameters/declarations, set the IDs assigned in the pre-pass. No ID mapping via the textual name after leaving the lexical scope.

### 9.2 Shared drop check

Add to `ownership_flow.h/.cpp`:

```cpp
bool mayDrop(const OwnershipPlan& plan, const FlowState& exit,
             OwnershipId id) {
    if (!exit.reachable || plan.suppressFunctionDrops || id == NoOwnership)
        return false;
    auto it = exit.slots.find(id);
    if (it == exit.slots.end()) return false;
    const OwnFact& f = it->second;
    if (f.origin != Origin::Owned || f.life != Live || f.resources.empty())
        return false;
    for (ResourceId r : f.resources)
        if (plan.suppressNewDrops.count(r)) return false;
    return true;
}
```

Before every `emitScopeDrop` in `visitBlockStmt`:

```cpp
const OwnershipPlan* plan = ctx.ownershipPlan;
if (!plan) {
    // Not analysed does not mean proven owned.
    // Diagnose centrally once per function; no new free here.
} else {
    auto ex = plan->normalExits.find(block);
    if (ex != plan->normalExits.end()) {
        for (auto it = frame.rbegin(); it != frame.rend(); ++it) {
            if (mayDrop(*plan, ex->second, it->ownershipId))
                emitScopeDrop(ctx, *it);
        }
    }
}
```

Important: `normalExits[block]` stores the state BEFORE removing the block-local slots, so that their drops can be checked. The state returned to the parent block removes them afterwards.

Free recipes must match the current slot type. An assignment can change owned to view, but not unnoticed the destructor kind of a slot. Detect overlapping recursive owner resources as a conflict before emission; `mayDrop` alone does not detect two aliases marked owned at the same time. The analyzer must lock this state or transfer ownership correctly.

### 9.3 Secure the function context

In `visitFunction` after the resolved signature, before `visitBlockStmt(func->body.get())`:

```cpp
OwnershipPlan plan = analyzer.analyze(*func); // new adapter interface
const OwnershipPlan* previous = ctx.ownershipPlan;
ctx.ownershipPlan = &plan;
// Body emission; use RAII for all exits in the actual implementation.
visitBlockStmt(func->body.get());
ctx.ownershipPlan = previous;
```

Also isolate `pendingParamDrops`, clean-up/temporary frames and the current symbol binding per function. The local plan must live until the end of the body emission. An RAII guard instead of a manual restore on only one success path is mandatory, especially with diagnostics and nested function generation.

### 9.4 Old consume helpers

After migration, `consumeLocalDrop` must no longer destroy a global frame. Switch all callers to the planned transfer: set, push, return, free/direct/interface calls, index/store paths. An empty helper without complete call site migration would be wrong too, because valid moves would then be dropped twice.

String temporary tracking stays separate, but must agree with the ownership plan. Do not let the already corrected direct consumed temporaries regress again.

## 10. Check matrix and sensible commit/change units without git commands

| Increment | Implementation | Acceptance |
|---|---|---|
| A | pure facts, IDs, join | unit checks for reachable/dead branches, mixed LifeBits, shadowing |
| B | AST/call origin, resource groups | the trace classifies `var_cb/gcb` as views of the same owner group |
| C | assignment plans | model copy stays independent; local list views are not freed; unknown cases clearly documented |
| D | if/while/exit analysis, conservative mode | no path-global removal; warnings deduplicated; fixed point terminates |
| E | normal drops plan-based | both reported regressions plus void/inner scope variants memory-safe |
| F | call retain and container links | shared tensor pattern preserves the owner; consumed temporary probes stay clean |

Propose new necessary probes under `tests/_mem_sanitize/`:

- `ownership_inner_view_void.aiml`: reproduces an inner list view with a normal function end; the return must not mask the danger.
- `ownership_branch_view.aiml`: one branch binds a view, the other owns its own list; the mixed merge must not produce a wrong drop.
- `ownership_branch_move_return.aiml`: THEN terminates, ELSE keeps running; its source stays correctly owned.
- `ownership_loop_outer_move.aiml`: an outer source in a repeated body; check zero/multi iteration.
- `ownership_shadowing.aiml`: slots of the same name have different IDs.
- `ownership_retained_model_param.aiml`: the callee stores a shared model; the owner is known at the caller.
- `ownership_reassign_view_to_owned.aiml`: a new real copy after a view; only the new own heap is droppable.

Keep checking the existing `test_list_list_inner_values` and `test_class_member_shared_tensor` unchanged. The latter writes tensor files with relative paths: use an isolated test working directory. Do not run any probe against productive 9rooms WALs.

The harness must prove functional success AND an instrumented complete run; no new UAF/double frees, no unchecked skip. Count suppressed drops together with their resource cause separately, so that "everything suppressed" does not count as a completed ownership implementation. The Q2 replace leaks stay listed individually.

## 11. Acceptance boundary and next package

This package is done when normal scope drops rest on proven ownership, branches/loops are no longer handled textually-globally, the two regressed patterns including normal exits are secured and unknown/retained resources are handled conservatively in a traceable way. A trace mode alone does not fulfil that.

Then take up P2a again separately: its return exit facts are already in `earlyExits`, but the return value and borrow returns must be secured before the active drops. Box/map drop remain separate follow-up packages, each with matching runtime contracts.

Server e2e is acceptance after the local safety proofs, not a substitute for them and not a precondition for implementing the analyzer at all. Check login, token, deckset, match and disconnect with an isolated data copy; do not modify production data as repro material.
