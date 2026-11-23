# Leak class 3: string local reassignment / self-accumulation (spec/61 §7)

As of: 2026-09-12. Volatile report. Line numbers apply to the working state read and
shift; code paths are additionally named by function name. All paths relative to the
AIML project root (`.../test/aiml`).

Context: hunting the 9rooms_v2 client leak (deckset editor, ~2 MB/min WASM heap growth). Via the
new diagnostic chain (memlog server + `emscripten_get_callstack` attribution in the
`AIML_MEM_DIFF` runtime + batched console→memlog forwarding), the leak was located completely in
`populate_card_full` (every frame per visible card row). Behind it are **three
separate compiler leak classes**. Classes 1 and 2 are fixed and green against `tests/run_memory_sanitizers.sh`.
**This report documents class 3 — still OPEN** — and the failed attempt at a solution.

---

## 1. The three leak classes (overview)

| # | Class | Example shape | Status |
|---|---|---|---|
| 1 | a producer string **bound to a local** is not dropped | `decl [x, string, {call: producer}]` | **fixed** (`freshOwned`+`visitDeclStmt`: `ownsReturnFns` call init → kind 1 drop) |
| 2 | a producer string as a **transient argument** is not dropped | `set_text(…, build_card_text(card))`, `gl.tex(mana_stem(col))` | **fixed** (`visitCallExpr`: producer string call result in `stmtTempStack` → decref at the end of the statement; `stringProducerFns`) |
| 3 | **string local reassignment / self-accumulation** does not free the old value; a **copy** from a borrow does not incref | `set:[acc, "${acc}…"]`, `decl [blk, string, line]; set:[blk, "${blk}…"]` | **OPEN** (this report) |

Class 3 is the **largest share** of the client leak.

---

## 2. What class 3 is exactly

### 2.1 Root cause

The plain string store in `visitSetStmt` (`src/core/ast_compiler_stmt.cpp`, branch `propName.empty()`,
`sym->typeName == "string"`) is a pure pointer store:

```cpp
ctx.ir_gen->createStore(val, baseAlloca);   // no incref of the source, no decref of the old value
```

The decl init store is analogous. As a result, string **local** assignment does **not** implement the spec/61 §7 model
(copy = incref/independent, move = transfer, reassign = free the old value). §7 says
explicitly that the former "shallow-share middle ground" was replaced — for **string locals**
it never was. Concretely, two defects:

1. **Reassign does not free the old value.** `set:[acc, new]` overwrites the slot without
   decref'ing the previous RC string → with a string accumulated in a loop, every
   iteration leaks the previous version.
2. **A copy from a borrow does not incref.** `set:[blk, member_or_local]` stores the raw
   pointer → `blk` **aliases** the source (no own RC ref). The source and `blk` would free the same buffer
   on drop (double free), or, if the source is dropped first, `blk` dangles
   (UAF).

Additionally, the ownership analyzer (`src/core/ownership_analyzer.cpp`, `analyzeStmt`, branch
`SetStmt`) conservatively marks a **rebound** plain local as `Origin::Unknown` ("rebound → not provably
owned"). `mayDrop` (`src/core/ownership_flow.cpp`) requires `Owned + Live + resources` → for a
reassigned string, `mayDrop` returns **false** → even the **final** value is not dropped at scope exit.
That is the deliberately postponed decision; cf. the note in `visitSetStmt` at the
local model deep copy: *"a reassigned target leaks the old heap (under-drop, safe) … Free-old-target
+ a fallible-copy contract come with the assignment-dataflow step."*

### 2.2 Where it occurs in the client (from the `AIML_MEM_DIFF` attribution)

All sites hang off `DeckEditorScreen_render → DeckEditorScreen_refresh_library_rows →
populate_card_full` (per frame, ~8 visible rows):

| Δ bytes / dump | Blocks | Call stack (leaf → …) | Class |
|---|---|---|---|
| +262080 | 3360 | `__aiml_str_concat` ← `CardArtCache_fetch` ← `art_ensure` ← `populate_card_full` | 3 (internal string construction) |
| +147350 | 1263 | `__aiml_str_concat` ← `ability_block_cached` ← `build_card_text` | 3 (internal `key`/`blk` construction) |
| +109039 | 5894 | `__aiml_str_concat` ← `mana_stem` ← `render_mana_cost_columns` | 2 (fixed via `producer`) |
| +40416 | 421 | `__aiml_str_concat` ← `build_card_text` (`out_txt`) | 3 |

The affected builders (`client/mock_card.aiml:build_card_text`/`ability_block_cached`,
`client/net_card_art_cache.aiml:CardArtCache_fetch`) build their string via
`set:[acc, "${acc}…"]` self-accumulation and/or `set:[blk, line]` (a borrow from a
loop local `line = generate_ability_text(...)`).

⚠️ **Safety-relevant for any fix:** on the cache hit path, `ability_block_cached` returns a
**cache view** (`return {call: g_abil_text_cache.get, key}`). This function must **not** be
annotated `producer` (the caller would decref the cache-owned string → cache corruption).
`build_card_text` can return `card.ability_text` (a borrowed member) — likewise not a
safe `producer` without incref-on-copy. Class 3 must therefore heal these functions via the
assignment path, not via return annotations.

### 2.3 Reproduction (headless, native)

`tests/_mem_sanitize/string_reassign_drop.aiml` (left in the tree, **not** registered in
`run_memory_sanitizers.sh`, since OPEN). Two subjects, native RSS delta:

- `loop_self_accum`: `decl [acc,string,"seed"]; while { set:[acc,"${acc}-seg-…"] }` — self-accumulation.
- `loop_share_accum`: `decl [blk,string,""]; while { decl [line,string,{call producer}]; if … set:[blk,"${blk}|${line}"] else set:[blk,line] }` — a mix of borrow copy and self-accumulation (the `ability_block_cached` shape).

**Baseline (parts 1&2, without the class 3 fix), N=300000, ~8 segments:**

```
string_reassign_drop: self-accum=230400000 B  share+accum=527990784 B  (limit=16777216 B)
```

(An earlier minimal probe `set:[acc,"${acc}…"]`, N=200000×8, yielded 281 MB; ≈176 B/reassignment,
i.e. exactly one leaked string per reassignment.)

---

## 3. How I tried to solve it (failed → reverted)

Goal: the complete spec/61 §7 model for string **locals**, purely additive, gated via the
sanitizer suite.

### 3.1 The four changes made

1. **`ownership_analyzer.cpp` `freshOwned`:** for `t == "string"` **always** `return true` (every
   string local is owned → gets a scope drop). Rationale: codegen should incref borrows when
   copying and move owned temps, so that the drop is always balanced.
2. **`ownership_analyzer.cpp` rebind:** introduced `slotType[id]` (from `DeclStmt::typeName`); on a
   plain local rebind, skipped the downgrade `Owned→Unknown` for **string** slots (the slot stays
   `Owned`, so that `mayDrop` drops the final value).
3. **`ast_compiler_stmt.cpp` `visitDeclStmt`:** registered a kind 1 drop for **all** non-`persistent`,
   non-`extern` string locals (instead of only interpolation/producer/`text_value`).
   Additionally **incref-on-copy** at the decl init: if the initializer is **not** an owned temp from the
   `stmtTempStack` (i.e. a borrow: variable/member/`at`/non-producer call/literal), incref the
   init pointer.
4. **`ast_compiler_stmt.cpp` `visitSetStmt` (plain string store):**
   - **incref-on-copy:** the RHS was not an owned temp (`rhsWasOwnedStringTemp`, checked **before**
     `consumeStringTemp`) → `aiml_str_incref(new)` (the local gets its own RC ref); owned temp =
     move (no incref).
   - **decref-old on self-reference:** if the RHS is a `StringInterpolationExpr` whose
     `parts` contain the target name (`set:[acc,"${acc}…"]`), decref the **old** slot value. Only
     this form, because here the liveness of the old value is guaranteed without a move data flow (reading an
     already moved value is a compile error).

   Helper functions `inStringTempStack()` (membership check before `consumeStringTemp`) and
   `emitStrIncref()` (magic-guarded `aiml_str_incref`).

Safeguards I relied on: `aiml_str_incref`/`decref` are magic-guarded
(`string_runtime.cpp`, no-op if `magic != 'STRN'` and for null/low pointers); every alloca is zeroed at
decl (`ast_compiler_stmt.cpp`, `CreateStore(getNullValue, alloca)`) → a no-init string is null
→ `decref(null)` is a no-op; self-reference guarantees the liveness of the old value.

### 3.2 What worked

- `producer_string_drop` (parts 1&2) stayed green.
- `string_reassign_drop` **self-accum**: 230 MB → **0 B**. `decref-old` on self-reference takes effect
  (fires unconditionally in codegen, independently of the ownership plan).

### 3.3 What failed (reason for the revert)

- **share+accum stayed at ~229 MB** (halved, but not solved). The final value (and partial paths)
  were not dropped despite "the slot stays owned".
- **Isolation probe `iso3` (build_a):** a function `decl [line0,string,{call producer}]; return
  line0.len` (scalar return, called in a loop) **leaks 128 B/iter**. I.e. the
  **return clean-up path** (`visitRetStmt` → `emitReturnCleanup`, mayDrop-gated) does not drop owned
  string locals in a called function — a deeper gap only exposed by the broad change
  (inline in the measuring frame, via the block exit drop, it works on the other hand —
  `loop_self_accum` = 0 B).
- **CRITICAL — safety:** under valgrind the full suite reported **`Invalid read of size 4`**
  (reading a 4-byte value = the RC header `magic`/`refcount` at `ptr-8`). So an incref/decref on a
  pointer whose `[-8]` is not mapped: a string local that holds a **non-RC pointer**
  (e.g. `req.body`, a buffer-as-string, a `.rodata` literal at a page boundary) or a
  **moved/freed** value that my unconditional incref-on-copy / decref-old touched.
  That is the double free/UAF class — not shippable.

Consequence: **all four changes reverted** to the green parts 1&2 state.

### 3.4 Why a naive solution is unsafe

- **Broad incref-on-copy** presupposes that every pointer written into a string local is an
  RC string. There are, however, string-typed values that have **no** RC header (raw
  malloc/strdup pointers, buffer views, some builtin returns). The magic guard catches a
  **wrong** header, but **not** a read at an unmapped address `ptr-8` (page boundary)
  or at an uninitialised/moved slot. → the actually observed `Invalid read`.
- **General decref-old** is only safe if the old value is **owned and not moved**. Without
  a move-aware data flow (push/return/consuming call mark the string slot as `Moved`),
  `push acc; set:[acc, …]` can decref the already given-away value again → double free. The
  self-reference subset is safe (the RHS reads the old value), but does not cover `set:[blk, line]`
  (borrow copy) and non-self reassignment.

---

## 4. What the correct fix needs (for the dedicated implementation)

This is the **"assignment-dataflow step"** postponed by spec/61 — deliberately a project of its own
that is hard to secure, not a bolt-on.

1. **Move-aware ownership data flow for string locals:** the analyzer must mark a string slot as
   `Moved` on push/return/consuming call (today only on a member store, `analyzeStmt`
   `SetStmt` branch). Only then may `decref-old` fire generally on reassignment (precondition:
   the old value is `Owned + Live`, not `Moved`).
2. **incref-on-copy only for provably RC-owned sources** — or a more robust guard that never performs the
   `ptr-8` read on unmapped/uninitialised addresses. Idea: separate RC strings from
   raw pointers on the type side, instead of relying on the magic guard alone. Alternatively an
   `aiml_str_is_rc(ptr)` with a safe, mapped probe (e.g. a registry instead of a header read).
3. **The return clean-up path** (`emitReturnCleanup`) must actually drop owned string locals in functions with
   a scalar/void return (a gap today — `iso3 build_a`).
4. **share+accum** (`decl [blk,string,""]; set:[blk,line]; set:[blk,"${blk}|…"]`, `line` a
   loop local) must be balanced end to end: `blk` gets its own ref on the borrow copy,
   the old `blk` is freed on the self-reassign, the final `blk` at scope exit.
5. **Gate:** the full `tests/run_memory_sanitizers.sh` (safety, no `Invalid read/free`) **plus** the
   9rooms suite (148/149). Register `string_reassign_drop.aiml` as a regression net once green.
   The broad change touches **every** string local (server, client, all tests) — stage it
   correspondingly carefully.

---

## 5. Artefacts

- **Reproduction:** `tests/_mem_sanitize/string_reassign_drop.aiml` (self-accum + share+accum),
  unregistered; a note on it in `tests/run_memory_sanitizers.sh` at the `producer_string_drop` entry.
- **Fixed & green (staying):** `producer_string_drop.aiml` (classes 1&2); producer annotations on
  `card_type_to_string`/`card_display_type` (`sources/models/card_types.aiml`) and `mana_stem`
  (`client/card_full_populator.aiml`, fixes the 109 KB site as class 2).
- **Diagnostic infrastructure (staying):** `AIML_MEM_DIFF` with WASM `emscripten_get_callstack` sites
  (`src/sys/mem_diff_runtime.cpp`), memlog server (`9rooms_v2/client/web/memlog_main.aiml`,
  `exodus-memlog.json`), batched console→memlog forwarding (`9rooms_v2/client/web/shell.html`).

---

# 6. Solution analysis & proposals (2026-09-12, addendum)

A deeper analysis of *how* class 3 can be solved safely. **None of this is implemented** — it is
the draft for the dedicated implementation. All statements are backed by repro measurements on the
reverted (parts 1&2 green) compiler.

## 6.1 Precise facts (proven)

Five separable mechanisms together form the leak. Important: they are **not** a
single bug.

1. **The string local store does not copy/move (the core of class 3).** The plain string store in
   `visitSetStmt` and the decl init store are pure `createStore(ptr, slot)`:
   - no `aiml_str_incref` when copying from a borrow → the target **aliases** the source;
   - no `aiml_str_decref` of the old slot value on reassign → every self-accumulation iteration
     leaks the previous version.
2. **The analyzer downgrades rebound locals to `Unknown`** (`ownership_analyzer.cpp`, `analyzeStmt`,
   `SetStmt` branch, "rebound → not provably owned"). `mayDrop` requires `Owned+Live+resources` → the
   **final** value of a reassigned string is not dropped at scope exit (deliberate
   under-drop).
3. **Return clean-up gap — the §6.2 owner exemption fires too broadly.** `emitReturnCleanup`
   (`ast_compiler_stmt.cpp`) exempts the owner of the return value from the clean-up (`return
   x.field` must not free x's heap). The base extraction, however, treats `return str_local.len`
   such that `retBaseId == str_local` — although `.len` yields a **scalar** i32 that cannot alias the
   string heap. Consequence: str_local is exempted and leaks.
   **Proven:** `return str_local.len` → 18.5 MB / 61 B/iter (no DROP in `AIML_TRACE_SYMBOL`);
   `decl [nlen,i32,str_local.len]; return nlen` → **0 B** (str_local is dropped). That is an
   **independent** bug, unrelated to reassignment.
4. **No move tracking for strings in the analyzer.** `life = Moved` is only set on a **member store**.
   push/return/consuming call only consume a string local at lowering
   (`consumeLocalDrop`), **without** marking the analyzer slot as `Moved`. That is why no
   later store knows whether the old value was already given away (and freed by the consumer).
5. **RC vs. raw strings.** Heap strings are RC (single chokepoint `aiml_str_alloc`, 8-byte
   `'STRN'` header); `incref`/`decref` are magic-guarded and null/low-pointer-safe
   (`string_runtime.cpp`). Literals are `.rodata` without a header (the guard applies, `[-8]` is
   mapped in practice — proven valgrind-clean with `lit_decref.aiml`). There are many raw `strdup`s
   in runtime structs (http_map, url parts, gltf), but mostly as internal fields; as an
   AIML string local they land in the middle of the heap → `[-8]` mapped → guard no-op, **safe**.

## 6.2 Why the attempt failed — diagnosis of the `Invalid read`

The `Invalid read of size 4` under valgrind was **not** a literal at a page boundary (ruled out with `lit_decref`),
but **reading an already freed RC header**: my unconditional
incref-on-copy / decref-old touched `[-8]` of a string that had previously been **moved and freed by the
consumer** (fact 4 — no move tracking). Valgrind reports the read on freed memory, although
the magic guard would neutralise it (because of the `magic=0` poison); after a realloc with a
coincidentally matching magic it would be a real double free. **Core lesson: every `incref`/`decref` on
a string local is only safe if the value is provably alive (not moved).** The
magic guard does not replace liveness knowledge.

Additionally, the attempt showed two "under-drop" gaps that the broad change only exposed:
share+accum stayed at 229 MB (final value not dropped) and the return clean-up gap (6.1/3).

## 6.3 Immediately safe, independent partial fixes (recommended as the first steps)

These do **not** depend on move tracking and can land + be gated individually:

- **(T1) Narrow the §6.2 exemption.** In `emitReturnCleanup`, exempt the owner **only** if the
  return type is a pointer/struct (can alias the owner heap). With a scalar return type the
  value is a copy → do **not** exempt the owner. Fixes fact 3 (proven: 0 B via temp). Purely
  additional dropping of already-owned locals → cannot free twice. **Risk: low.**
- **(T2) decref-old only on self-reference.** `set:[acc,"${acc}…"]`: the old value is **read** in the RHS
  → provably live → safe to decref; the new value is a distinct fresh
  concat. Fixes pure self-accumulators (proven: self-accum 230 MB → 0). **Precondition for
  safety:** the accumulator must not be a borrowed alias — holds if its init is a literal/
  owned temp (the `build_card_text`/`ability_block` `key` case). For the `blk=line` borrow case,
  T2 alone does not apply (see core options). **Risk: low-medium**, secure with a probe.

T1+T2 together address the pure self-accumulators and the scalar return leaks — a
measurable part of the client leak — **without** the risky broad switch-over.

## 6.4 Core options for the complete solution

### Option A — deep copy instead of a shared refcount on copy
A string local copy from a borrow does `aiml_str_dup` (fresh rc1) instead of sharing via `incref`. Every
local owns a **unique** buffer → no aliasing, `decref`-old semantically simple.
- **Pro:** eliminates the aliasing class entirely; no shared refcount fine-tuning.
- **Con:** does **NOT** solve the move problem (a moved unique buffer, freed by the consumer,
  then touched by `decref`-old → the same UAF). Additional copying costs per
  assignment. **Insufficient on its own.**

### Option B — a complete move-aware ownership data flow for strings (recommended end state)
Bring the analyzer to a per-slot lattice that **also** marks push/return/consuming call as `Moved`
(close fact 4). Then:
- `decref`-old on reassign only if the pre-set state is `Owned+Live` (not `Moved`);
- `incref` (or dup) when copying from a borrow;
- the slot stays `Owned` after reassign (the earlier analyzer change — now **correct**, because the move
  is tracked);
- return clean-up uses the same facts (subsumes T1).
- **Pro:** the one, consistent, spec/61 §7-faithful solution; fixes all three sub-gaps.
- **Con:** touches **every** string local (server, client, all tests); the analyzer extension
  (move across calls/push/return) is the actual effort. **Risk: high** — only introduce it staged with the full
  sanitizer suite **and** 9rooms 148/149.

### Option C — region/arena for transient strings (rejected)
A per-frame/scope arena, freed wholesale. Bypasses individual ownership, but collides with the existing
RC model and long-lived (member) strings. Does not fit the rest, too invasive. **Rejected.**

### Addition to B — separate RC cleanly from raw pointers
The `Invalid read` shows: relying on the magic guard alone is fragile (a read on freed
memory). More robust would be making RC strings reliably recognisable without blindly reading `[-8]` —
e.g. a separate type/tag path or a mapped registry instead of a header probe. That also defuses
future cases in which a string local holds a raw pointer.

## 6.5 Recommended staging

1. **T1** (narrow the return clean-up exemption) — small, safe, immediately measurable; its own probe
   (`iso4/iso5` pattern → register).
2. **T2** (self-ref decref-old) — covers pure self-accumulators; `string_reassign_drop`
   `loop_self_accum` as the gate (already 0 B in the attempt).
3. **Option B** as the final, coupled step: analyzer move tracking for strings +
   incref-on-copy + decref-old(Owned+Live) + return clean-up via facts. Only when `string_reassign_drop`
   brings **both** subjects (self-accum **and** share+accum) to 0 AND the full sanitizer suite +
   9rooms suite are green. The share+accum shape (`decl blk=""; set blk=line; set blk="${blk}|…"`)
   is the acceptance test for the coupling.

## 6.6 Affected code places (anchor list for the implementation)

- `src/core/ast_compiler_stmt.cpp` — `visitSetStmt` plain string store (`createStore(val, baseAlloca)`);
  decl init store + `consumeStringTemp`; `visitDeclStmt` string drop registration; `emitReturnCleanup`
  + the `retBaseId` extraction (T1); the `visitBlockStmt` mayDrop gate.
- `src/core/ownership_analyzer.cpp` — `freshOwned` (string case); `analyzeStmt` `SetStmt`
  (rebind downgrade, move marking); add move marking for push/return/consuming call (B).
- `src/core/ownership_flow.cpp` — `mayDrop` (Owned+Live+resources), the basis of all drops.
- `src/sys/string_runtime.cpp` — `aiml_str_incref/decref` (magic-guarded); possibly harden RC detection.
- Reproduction/gates: `tests/_mem_sanitize/string_reassign_drop.aiml`; a new probe for the T1 pattern.

---

## 6. Supplementary analysis: concrete findings and code solutions

The following proposals were checked against the current source state. **They are not yet built into the
compiler.** Existing measurements from sections 1–5 remain historical findings; only the small
return clean-up probe in §6.1 was newly run here. The snippets are split into a
local patch proposal and an architecture/integration draft. New helper names are explicitly
proposals, not already existing APIs.

### 6.1 Separate small fix: do not exempt the owner of a scalar return

**Concrete place:** `src/core/ast_compiler_stmt.cpp`, `Compiler::visitRetStmt`, currently
lines 1636–1665; in particular the unconditional exemption in `emitReturnCleanup` at line 1663.

The code derives the base name from every `VariableExpr`:

```cpp
std::string base = rv->name;
size_t dot = base.find('.');
if (dot != std::string::npos) base = base.substr(0, dot);
if (Symbol* bs = resolveVariable(base)) retBaseId = bs->ownershipId;
```

That makes **line0** the exempted owner for `return line0.len`. `retScalar` is true
and activates clean-up, but the inner loop nevertheless skips exactly this string:

```cpp
if (it->ownershipId != NoOwnership && it->ownershipId == retBaseId) continue;
```

That is a concrete explanation for `iso3`, not a general inability of the scalar return path.
The function already computes the length with `visitExpression` into an SSA result **before**
`emitReturnCleanup` runs. Afterwards the string may be freed.

**Patch proposal, directly at the exemption:**

```cpp
// Scalar/void returns do not transfer or borrow the owner's heap.
const bool returnMayAliasHeap = retTy &&
    (retTy->isPointerTy() || retTy->isStructTy());

// Inside emitReturnCleanup, replace the unconditional owner exemption:
if (returnMayAliasHeap && it->ownershipId != NoOwnership &&
    it->ownershipId == retBaseId)
    continue;
if (mayDrop(*retPlan, *retExit, it->ownershipId))
    emitScopeDrop(ctx, *it);
```

The existing `safeToCleanup`/`mayDrop` barriers are preserved. No blanket
`emitScopeDrop` for all strings and no change to pointer/model-valued returns.
The scalar classification corresponds to the existing AIML rule; explicit pointer→integer
escapes would have to be treated as a language/escape problem of their own.

**Newly verified, unchanged compiler:** a temporary probe under
`AIML_LIFETIME_DROPS=1 AIML_TRACE_SYMBOL=line_direct,line_expr AIML_CACHE=0`:

```yaml
- def:
    name: make_line
    args: [[num, i32]]
    return: { type: string, lifetime: producer }
    body: [{ return: "payload-${num}" }]
- def:
    name: direct_length
    return: i32
    body:
      - decl: [line_direct, string, { call: [make_line, 1] }]
      - return: line_direct.len
- def:
    name: expression_length
    return: i32
    body:
      - decl: [line_expr, string, { call: [make_line, 2] }]
      - return: { +: [line_expr.len, 0] }
```

Both functions return the same length and the probe ends with exit 0. The trace contains
`DECL line_direct`, `DECL line_expr`, **only `DROP line_expr`**. Both DECLs report
`cleanup=registered`. That isolates the wrong owner exemption, without reassignment or cache.
Probe/log of this analysis: `/tmp/aiml_string_return_cleanup_probe.aiml` and `.log`; these
temporary files are not a permanent regression net. On implementation, take them over into `tests/_mem_sanitize/`
and add `return model.scalar_field`, `return list.len` and a still valid
heap/view return as a counter-probe.

### 6.2 Recommended larger solution: a string local holds an RC reference or null

**Target invariant for migrated local string slots:**

> A slot contains either `nullptr` or an own, valid reference to an
> `aiml_str_alloc` allocation. Borrowed/raw/static inputs are normalised before storing.
> A move leaves null. A scope drop stays registered for the slot.

This does not claim that all values typed as `string` today are RC strings.
It introduces a **new boundary, explicitly enforced by codegen** for local bindings.
`persistent`, `extern`, globals and borrowed/by-ref parameters are excluded at first.
Taking over these categories needs its own lifetime rules for each.

**Why duplicate at first instead of incref everywhere?** `aiml_str_dup` in
`src/sys/string_runtime.cpp:65` reads the valid string content and creates an RC string;
it reads no header in front of a foreign address. A borrow must still be
alive and NUL-terminated at the copy point. Copying does not repair an already dangling pointer either.

New runtime helpers, proposed insertion point at `aiml_str_dup`/`aiml_str_decref` in
`src/sys/string_runtime.cpp`; declarations in `src/sys/string_runtime.h`:

```cpp
// PRE: dst is a managed local slot: null or one owned RC reference.
// PRE: src is a live NUL-terminated borrowed string, or null.
// Returns 0 on allocation failure, leaving dst unchanged.
extern "C" int32_t aiml_str_assign_copy(char** dst, const char* src) {
    char* next = aiml_str_dup(src);       // acquire BEFORE releasing old
    if (!next) return 0;
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);                // safe by dst invariant, not magic guessing
    return 1;
}

// PRE: next transfers a separate owned RC reference, or null.
// A self-assignment with no transferred reference is excluded by the caller.
extern "C" void aiml_str_assign_move(char** dst, char* next) {
    char* old = *dst;
    *dst = next;
    aiml_str_decref(old);
}

// Use at a real ownership-transfer point, after capturing the value.
extern "C" char* aiml_str_take_local(char** src) {
    char* value = *src;
    *src = nullptr;
    return value;
}
```

These helpers are safe **only for normalised slots**. They must not be applied to existing
raw/borrowed member slots. `aiml_str_assign_move(dst, next)` also needs
proven RC ownership; the label `producer` alone is only sufficient if the
respective return contract actually guarantees RC-managed results.

For `next == old`: two own RC references to the same address are permissible; then
the old reference must nevertheless be subtracted. `set:[x,x]` without a second reference, on the other hand, stays
a no-op at the existing self-assignment guard. A mere pointer equality guard in the helper
would mix these two cases inadmissibly.

**Allocation failure:** `assign_copy` must not silently pretend a successful store.
On a return of 0, codegen must enter the defined error path; the old slot stays
intact until then. For owned temporaries, their clean-up stays responsible until the successful transfer.
A null return value from a producer is not to be equated with OOM without its contract.

**Integration places:**

| Place in the current tree | Change |
|---|---|
| `ast_compiler_stmt.cpp:446–486`, string drop registration | Register a drop for explicitly normalised local slots; do not first declare all strings globally as owned. |
| `ast_compiler_stmt.cpp:640–670`, initializer evaluation / `consumeStringTemp` | Capture the owned temp property before consuming. Duplicate a borrow input; take over a proven OwnedRC temp. A no-init slot stays null. |
| `ast_compiler_stmt.cpp:926`, generic init store | Perform the string normalisation before it and do not emit a second generic store. |
| `ast_compiler_stmt.cpp:1054–1101`, SelfAssign / generic `consumeLocalDrop` | Branch off a plain string local copy before the generic move path; `set:[blk,line]` must not consume line. |
| `ast_compiler_stmt.cpp:1132`, `consumeStringTemp` | For the new path, consume the temp only after a successful takeover. |
| `ast_compiler_stmt.cpp:1321`, generic plain local store | For managed string slots, `assign_copy` or `assign_move`; free the old slot independently of syntactic self-reference. |
| `compiler.cpp`, `jitRun` `AddSymbol` block | Register the new runtime helpers for the JIT; rebuild the native/WASM runtime together. |

**Codegen sketch; new helper/metadata names are a draft:**

```cpp
// This branch must precede generic consumeLocalDrop(value).
if (isManagedStringLocalTarget(*sym, propName, isDash)) {
    if (sym->is_const) { reportConstAssignment(); return; }
    if (isExactSelfAssignment(setStmt, baseName)) return;

    IRValue* rhsIR = visitExpression(setStmt->value.get());  // old value still alive
    if (!rhsIR) return;
    auto* rhs = static_cast<llvm::Value*>(ctx.ir_gen->unwrap(rhsIR));
    const bool ownedRC = isOwnedRCStringTemp(rhs);  // BEFORE consumeStringTemp

    if (ownedRC) {
        emitAssignMove(baseAlloca, rhs);
        consumeStringTemp(rhs);                    // after successful transfer
    } else {
        emitAssignCopyOrFailure(baseAlloca, rhs);  // dup, store, decref old
        // Do NOT consume the source local's cleanup.
    }
    return;
}
```

`isOwnedRCStringTemp` needs more than the AIML type name: temporary ownership and
the RC representation must be proven. A borrow return of unknown origin is not a temp move.
The existing `stmtTempStack` entries can be used for already guaranteed RC producers;
in the long run the classification should hang off the result of the resolved call.

**Effect on the repro:** `blk = line` duplicates the source; `line` may drop at the end of the iteration.
`blk = "${blk}|${line}"` first computes the new RC string, replaces the slot and frees the old
`blk`. The final `blk` is freed at the end of the block. `acc = producer()` without
self-reference and `acc = "literal"` also get the same safe reassign path.
The price is initially an additional copy for borrowed RC strings; the optimisation to
incref only comes after proven origin (§6.5).

### 6.3 Moves and control flow: do not delete the drop globally from the clean-up list

**Concrete places:** `ast_compiler_stmt.cpp:1799`, `consumeLocalDrop`; normal drops from
line 2034; return drops from line 1657; `ast_compiler_call.cpp` for consumed arguments,
`list.push` around line 1341 and `map.put` around line 1576. Today clean-up entries are deleted there
at compile time. For conditional model moves there is already a
comment/guard on this problem around line 1304; that is not a general string solution.

```yaml
- decl: [acc, string, { call: [producer] }]
- if: { condition: flag, then: [{ call: [sink.push, acc] }] }
- set: [acc, { call: [producer] }]
```

On the true path the first value belongs to the sink; on the false path still to `acc`.
A clean-up removed globally at code generation cannot express that. Moreover,
the newly assigned value needs a clean-up again, even if a move happened before.

**For managed string slots:** keep the clean-up entry for the entire slot lifetime;
on a real move in the executed path, take over the value and null the slot.
Later reassignments then need no re-attached scope drop.

```cpp
// Lowering sketch for a known, committed consuming operation:
auto* transferred = emitTakeManagedStringLocal(sourceSlot);  // load + store null
emitConsumingSink(transferred);                              // sink receives captured value
// cleanupStack entry remains; sourceSlot is null on this runtime path.
```

Do **not** simply build `CreateStore(null, addr)` into today's `consumeLocalDrop`:
it is called, among other places, before `visitExpression`/before the actual argument load.
That would pass on null instead of the string. The take must sit at the evaluated value and the
confirmed transfer. With fallible/throwing sinks, ownership is to be secured until the commit via
temporary clean-up/an unwind guard, or the slot restored on abort. A
`consumed` contract must define whether the callee also takes ownership on error.

Calling a merely borrowing/retaining sink is **not a move**. A stored
borrow must get its own reference/copy or be diagnosed as an inadmissible escape;
nulling must not disguise unknown retention behaviour.

**Drop sketch for these explicitly marked slots:**

```cpp
// New ScopeDrop flag, set ONLY after the canonical-RC-slot lowering is complete:
if (drop.managedStringSlot) {
    auto* value = builder.CreateLoad(ptrTy, drop.addr);
    builder.CreateStore(llvm::ConstantPointerNull::get(ptrTy), drop.addr);
    builder.CreateCall(strDecref, {value});
} else {
    // Existing mayDrop + legacy emitScopeDrop behavior stays in force.
}
```

The null invariant can safely clean up a mixed runtime path `Live|Moved`. This
special handling may only be activated once **all** write/move/escape paths of the slot
are migrated. Do not loosen `mayDrop` for arbitrary unknown/borrowed strings. Unclarified aliases,
`persistent`, external mutation or unmodelled exceptions exclude the slot at first.

### 6.4 Analyzer: stable reassign resources and shared call effects

**Places:** `ownership_analyzer.cpp:130` (`assignIdsStmt`), `:195–245` (SetStmt),
`:250–260` (while fixed point), `:265–273` (return and previously ignored call effects);
`ownership_flow.h`, `StatementPlan`/`OwnershipPlan`; `ownership_flow.cpp:55`, `mayDrop`.

The analyzer must describe a normalised rebind as a new own value, instead of merely
omitting the previous `Owned→Unknown` downgrade. In particular, the resource set
must not stay empty if the slot was originally a literal/borrow.

```cpp
// NEW members in Analyzer; allocated in pass 1, never inside the loop fixpoint:
std::map<OwnershipId, std::string> slotTypes;
std::map<const SetStmt*, ResourceId> assignmentResources;

// assignIdsStmt, declaration branch:
slotTypes[id] = d->typeName;

// assignIdsStmt, NEW SetStmt branch; one stable abstract reference per source site:
if (auto* set = dynamic_cast<const SetStmt*>(s))
    assignmentResources.emplace(set, nextRes++);

// analyzeStmt(SetStmt), after resolving target and validating normalized lowering:
if (isManagedStringSlot(id) && !selfAssign) {
    OwnFact next;
    next.origin = Origin::Owned;
    next.life = Live;
    next.resources = {assignmentResources.at(set)};
    st.slots[id] = next;
    auto& sp = plan.statements[set];
    sp.target = id;
    sp.result = next;
    // Add explicit string-copy / temp-adopt variants to Transfer;
    // do not misuse generic DeepCopy if codegen treats it as model memcpy/serialize.
}
```

`ResourceId` here stands for the abstract **own reference/creation site**, not for a
physical address constant across the whole loop. For a later incref optimisation,
RC reference ownership and shared buffer identity must be kept apart. Always new IDs on every
analysis pass would prevent fixed-point convergence.

Analyzer and codegen should read call effects from the same resolved signature:

```cpp
// Proposed resolved-call metadata, shared by analysis and lowering:
enum class StringResultKind { Borrowed, OwnedRC, Unknown };
enum class ArgumentEffect { Borrow, CopyIntoSink, Consume };
```

That way consumed parameters, `push`, `put`, return and nested call expressions
are handled consistently. The current catch-all at the end of `analyzeStmt` ignores call moves.
Calls in initializers, set RHS, conditions and return expressions also need effects, not
only `ExprStmt`. `MemberStore` may only plan a move for `Owned` **and exactly `Live`**.
Using a `Moved` or `Live|Moved` string needs a diagnostic or a permissible
reinitialisation path.

**Additional concrete drift:** `visitDeclStmt:472` knows `text_value`/`dropdown_value` as
owned string builtins; `freshOwned` in the string branch does not name them explicitly. Whether they are captured via
`ownsReturnFns` must be proven per resolved call. Replace the duplicate
short-name heuristic with shared result metadata; a getter of the same name
in another class must not mistakenly count as a producer.

### 6.5 Alternative/optimisation: safe RC proof instead of a header probe

`string_runtime.cpp:88–108` already reads `s - sizeof(AimlStrHeader)` before the magic check.
A numeric low-pointer check or `magic == STRN` does not make this access safe.
A blanket mapping probe also does not protect against an already freed block,
address reuse or concurrent freeing.

**Alternative A, the preferred later optimisation:** store proven provenance on the IR result.
Only use `incref` for `OwnedRC` or a provably live borrow of an RC owner;
for `BorrowedRaw`/`Unknown` keep duplicating. An own RC ref to the same buffer is
sufficient for `blk` and saves the content copy compared to §6.2.

**Alternative B, a bigger runtime change:** an always active registry of all live RC string
base addresses, with insertion in `aiml_str_alloc` and removal at the last `decref`.
The lookup must happen **before** any header is read:

```cpp
// Sketch inside a locked RC-registry implementation:
auto it = liveRC.find(s);                // pointer key only, no dereference of s[-8]
if (it == liveRC.end()) return false;
AimlStrHeader* hdr = it->second;
++hdr->refcount;
return true;
```

Do not misuse the optional `AIML_MEM_DIFF` registry for this: lifetime safety must
not depend on a diagnostic flag. Registry synchronisation, allocation errors,
process/thread shutdown and native/WASM costs must be solved separately. A registry hit
still does not prove the validity of a stale pointer after address reuse (ABA).
Data flow/lifetime rules remain necessary. Therefore this variant is not a small replacement line
for today's magic guard.

### 6.6 Mixed returns and cache put: a separate part of the solution

**Current places in 9rooms (only read here, no change):**
`../9rooms_v2/client/mock_card.aiml:366–395` (`build_card_text`),
`:404–440` (`ability_block_cached`). Corrected path to the art cache file:
`../9rooms_v2/client/net/card_art_cache.aiml`.

The cache miss currently ends with:

```yaml
- call: [g_abil_text_cache.put, key, blk]
- return: blk
```

A put that consumes `blk` and the subsequent return of the same local value are
two ownership takers. A class 3 fix must not turn this into an empty return by blind nulling.
One of the following contracts must apply explicitly:

1. **Borrowed cache return:** the cache takes over `blk`; afterwards the cache view
   is returned while the cache stays valid. The lookup key must itself be valid,
   although `put` can consume it too. That is not a producer return.
2. **OwnedRC return:** cache and caller each get an own reference or copy. A cache hit
   duplicates/retains the cache view before the return; a cache miss copies/retains for the cache
   and transfers an independent reference to the caller. Only then is `producer` correct.

Concrete C++ semantics for variant 2, as a contract example, not as an existing cache API:

```cpp
// Assume borrowed_cache_lookup returns a live borrowed char*.
if (const char* hit = borrowed_cache_lookup(key))
    return aiml_str_dup(hit);             // caller owns a NEW RC reference

char* result = build_owned_text();        // one owned reference
char* cacheCopy = aiml_str_dup(result);    // second owned reference
if (!cacheCopy) {                         // handle failure without losing result
    aiml_str_decref(result);
    return nullptr;                       // only if contract defines failure as null
}
cache_put_consuming_value(key, cacheCopy); // key ownership handled separately
return result;
```

The calling compiler must treat all return paths as `OwnedRC` and drop the return
or transient call results accordingly. Merely adding `producer` to the
current cache getter stays wrong. An unannotated mixed return cannot be completely balanced
by local reassignment alone either: if a built string
escapes and counts as a borrow at the caller, the last reference stays lost.

This refines the statement from §2.2: **the assignment path must be corrected, but is not enough on its own for
complete end-to-end release across mixed return boundaries.**
An unknown malloc-based "borrow" return that in truth transfers ownership first needs
a correct contract; duplicating alone would leak its original buffer.

## 7. Recommended implementation and acceptance

1. **Correct the scalar return exemption (§6.1) in isolation.** A small native/WASM repro and Valgrind;
   that removes the clear `line0.len` error before the broad change.
2. **Centralise RC/borrow contracts; mark normalised slots separately.**
   Switch decl, reassign and copy together, do not anticipate `freshOwned(string)=true`.
3. **Migrate move/return/container boundaries for these slots.** Keep the clean-up entry,
   only empty the actually transferred slot; analyzer and codegen use the same effects.
4. **Close cache hit/miss and mixed return contracts.** No standalone
   producer annotations without a corresponding change of all return paths.
5. **Correctness first, then the RC incref optimisation instead of a borrow copy.**

In addition to the existing large `string_reassign_drop` probe, small targeted cases are needed:

| Case | Expectation |
|---|---|
| producer local → `return local.len` / `return model.scalar` | owner dropped after the scalar is determined |
| `x=x` and two slots with the same RC buffer | no accidental release; references balanced correctly |
| borrow of a literal, a `strdup` buffer, the valid interior of a buffer | no read in front of the source pointer; the copy stays valid after the source is freed |
| page boundary probe with a protected previous page (native) | normalising reads only the string content, no header probe on a borrow |
| `blk=line`, the source leaves the inner scope | `blk` stays valid; both own references are released |
| reassign after a guaranteed/conditional move | the sink stays valid, the newly assigned slot is dropped later |
| conditional move without a subsequent reassign | the true path drops null, the false path the original slot |
| empty loop / several back edges / shadowing | no wrong drops; analyzer fixed point stable |
| failed borrow copy / fallible consuming sink | the old value or the temporary owner stays correctly responsible |
| cache hit/miss, then cache eviction | no double release or lost return reference |
| lifetime feature off | the previous off mode unchanged |

For reproducible leak measurement, use a warmup and fixed inputs; look at allocator/live allocation
deltas in addition to RSS. Valgrind checks safety and allocation stacks; treat compiler/JIT
baseline losses separately. Do not rate the large RSS probe as a leak measure under Valgrind.

On implementation: **active `exvenv`, `exodus build --all`**, targeted native/WASM tests,
the full `tests/run_memory_sanitizers.sh`, then the relevant 9rooms acceptance. Only add the
so far unregistered class 3 test to the gate after a green implementation.

---

# 8. Implementation status (2026-09-12, addendum 2)

Implemented and green against the **full** `tests/run_memory_sanitizers.sh` (0 safety violations):

- **T1 — scalar return owner exemption narrowed** (`emitReturnCleanup`, `returnMayAliasHeap`): the
  owner is only exempted for pointer/struct returns now. `return s.len` now drops the
  owned string local. Probe `tests/_mem_sanitize/return_cleanup_scalar_drop.aiml` (registered):
  scalar return 0 B, heap return 0 B.
- **T1b — a direct `return "${…}"` did not free its interpolation INTERMEDIATES** (a new bug discovered
  with the T1 probe): the `createRet` terminator skipped the per-statement temp decref
  loop in `visitStatement`. Fix in `visitRetStmt`: decref the remaining `stmtTempStack` temps before the
  terminator (the final return value was removed beforehand via `consumeStringTemp`). Probe
  `tests/_mem_sanitize/return_interp_temp_drop.aiml` (registered): 0 B (previously ~18–24 MB).
- **Runtime helpers laid as the foundation** (`string_runtime.{h,cpp}`, JIT-registered in
  `compiler.cpp`): `aiml_str_assign_copy` (dup + decref-old), `aiml_str_assign_move` (store +
  decref-old), `aiml_str_take_local` (load + null). **NOT yet called by codegen** — the foundation
  for the core step. No behaviour change; build green.

**CORE LANDED (2026-09-12, addendum 3) — managed string local model, full sanitizer suite green
(0 safety violations, 0 FAILs, the `Invalid read` gone):**

- `freshOwned(string)=true`; the analyzer keeps managed string slots owned across a rebind with a stable
  decl resource (`ownership_analyzer.cpp`, `slotType`).
- `visitDeclStmt`: a null-safe kind 1 `managedStringSlot` drop for every managed string; the init is
  normalised (borrow → `aiml_str_dup`, owned temp → move).
- `visitSetStmt` (plain local): `aiml_str_assign_move` (owned temp) / `aiml_str_assign_copy`
  (borrow, dup + decref-old) → the old value is freed.
- Member store: a managed string local source is copied via `incref` (the source stays owned);
  its `consumeLocalDrop` at the pre-load set source site is skipped.
- Move sites: `consumeLocalDrop` nulls the managed slot + keeps the drop (post-capture:
  push/put/consuming arg); return uses erase mode (pre-load, owner exempt); the managed scope drop is
  `decref(load); store null` (null-safe on a Live|Moved merge).
- `aiml_str_assign_copy/move/take_local` in `string_runtime.{h,cpp}`, JIT-registered.

**Result:** `string_reassign_drop` self-accum **0 B**, share+accum **0 B** (previously 230/528 MB);
all regression probes green; probe registered. **Still open:** the 9rooms acceptance (148/149) — the
core touches EVERY string local, therefore mandatory before "done".

Historical rationale for why the core is only safe as **one coupled unit**:

- Exact facts from the implementation preparation: there are **~7 `consumeLocalDrop` move sites**
  (set store into a member/container `ast_compiler_stmt.cpp:1101`, return `:1618`, consuming args
  `ast_compiler_call.cpp:423`/`:737`/`:2581`, `list.push` `:1341`, `map.put` `:1576/:1577`). For a
  managed string slot, **every** site must null the slot after capturing the value (take), otherwise
  a later decref-old/scope drop decrefs an already moved (freed) value →
  double free.
- **Timing trap:** the set store site calls `consumeLocalDrop` **before** `visitExpression` of the value
  (the value load only at ~`:1146`). Nulling in the central `consumeLocalDrop` would pass `null` to the sink there.
  So the nulling must sit **per site after the capture**, not centrally — exactly the
  "migrate ALL paths" requirement from §6.3.

A first broad attempt (all strings owned + incref-on-copy, without this move site coverage)
produced the `Invalid read` (§6.2). That is why the core is implemented as a unit of its own with complete
coverage (dup-on-copy instead of incref; take+null at all 7 sites; the analyzer keeps managed
strings owned with a stable per-set resource; the scope drop `decref(load)` is null-safe). Acceptance:
`string_reassign_drop` **both** subjects → 0 AND the full sanitizer + 9rooms suite green, then register
the test.

---

# 9. Open match crash: producer short name collision (2026-09-13)

**Symptom:** server crash (`malloc(): unaligned tcache chunk detected`, signal 6) at match start,
**only** after having been in the deck editor. Under valgrind no abort (own allocator), but valgrind
reported **8× invalid free() + 9× invalid read**.

**Root cause (PRE-EXISTING, NOT the class 3 model):** the valgrind stack showed
`__free_GetLobbyHandler ← RpcDispatcher_dispatch`, freeing a block that was allocated ONCE in
`GameServer_install_handlers` (`{new: …}`). The dispatcher does:
`- decl: [hnd, ptr<mirror.RpcHandler>, { call: [<.handlers.get, rpc.method] }]` — a **map borrow**
of the registered handler. The return ownership classification (`freshOwned` ptr<> interface box)
matched producers only by **short name**: there are `producer` methods named **`get`**
(deck/lobby/match/deckset service → `*Resp`), so `"get" ∈ ownsReturnFns`. As a result `handlers.get`
counted as an owned interface box → **kind 5 drop on EVERY dispatch** → the persistent handler was
freed; repeated dispatches (the deck editor polls `net_list_*`) → double free → a later
tcache abort at match start.

**Fix:** match producers by **return type**, not only by name. New: `ctx.ownsReturnFnTypes`
(short name → set of declared return types) + `ctx.ownsReturnMatches(shortName, declType)`. All
four `freshOwned` branches (list/model/box/string) and the lockstep drop registrations in
`visitDeclStmt` use it. `handlers.get` (`ptr<mirror.RpcHandler>`) no longer matches the `get` producers
(`*Resp`) → `hnd` no longer owned (`cleanup=unregistered` confirmed via AIML_TRACE_SYMBOL)
→ no drop → no double free. `get_base` (a real `ptr<CardBase>` producer) still matches correctly.

**Gate:** the full `run_memory_sanitizers.sh` green (0 safety violations), incl. producer_list/get_base/
string_reassign/producer_string. The class 3 kill switch stays **ON**.

**Lesson:** the short name match (spec/61 §6.4) is collision-prone; the return type check is the
robust version. Container builtins (`map/list/kv .get/.at`) deliver borrows and must never be classified as
owned.

# 10. Container string ownership: the "no mana / broken event logs" regression (2026-09-13)

**Symptom:** after activating the managed string model (class 3): "suddenly you no longer produce
mana, and the event logs are odd to broken". The first suspicion (null-on-move →
dup-on-move in `consumeLocalDrop`) did NOT apply — the cause lay deeper, in the containers themselves.

**Reproduction (isolated probes, no guesswork):**
- `mana_pool_key_drop.aiml` (mirrors `ManaPool`): `total=20,000,000` instead of `2,000,000` — **exactly 10×**.
  All 10 mana colour keys `"1".."10"` **collapsed into one map slot**.
- `event_log_push_drop.aiml` (mirrors `log_event`): `bad=200,000` — **every** pushed line was
  unreadable (dangling) after the helper returned.

**Root cause — containers stored BORROWED string pointers:**
Both `MapImpl::put` (keys/values) and `list<string>.push` (elements) stored the `char*`
**raw** (no dup). That was "ok" as long as strings were never freed (a leak, but every pointer
distinct). With the managed string model, string locals are **freed** at scope exit →
the freed address is reused in the next iteration → all container entries point to
the same (last written) buffer → collapse (map) or dangling (list).

The previous "MOVE into the container + suppress the caller drop" strategy (`consumeLocalDrop`/
`consumeStringTemp`) ONLY works if `put`/`push` is in the **same scope** as the string decl.
Across a helper (`ManaPool.add_color → colors.put`; `log_event(msg) → lines.push(msg)`)
the real owner is one frame higher, out of reach of the suppression → it frees the container string.

**Fix (spec/61 §8) — containers OWN their strings (dup-on-insert, free-on-remove/drop):**
1. `MapImpl::put`: dups string **keys** (append) and string **values** (append + decref-old on
   update). New param `valIsString`. Non-string values (model/ptr) stay MOVE.
2. `MapImpl::remove`: decrefs the removed string key/value before the memmove.
3. `list<string>.push`: dups the element (`aiml_str_dup`) — replaces+unifies the old
   split paths (MOVE for locals/temps, incref for member strings) into one robust rule. Non-string
   elements (model/ptr/interface) stay MOVE/DeepCopy.
4. `ListImpl::remove`: decrefs the removed string element (fixes the churning `event_log` 40-line
   ring leak: 8.6 MB → 65 KB).
5. `aiml_map_free`: now walks **densely** over `[0,size)` instead of over the vestigial `occ` bitmap
   (which `put` never wrote; the `[0,capacity)` occ walk after a grow was the old
   "Invalid read of size 1" that blocked the map drop).
6. Caller suppression removed: a string key/value (map) and a string element (list) are now pure
   **borrows** — the caller keeps its scope drop/stmt temp and frees its own copy,
   no matter how many frames away it was built.
7. Deserialisation (`serialize_impl`): `aiml_jr_map_key/_val_str` return owned dups; since `put` now
   dups, the reader copy is freed after `put` (also fixes a pre-existing leak, incl. the
   `ks` int key string).

**Result (LIFETIME_DROPS=1):** `mana_pool_key_drop` `total=2,000,000` ✓, `event_log_push_drop`
`bad=0` ✓, both with bounded RSS. The full `run_memory_sanitizers.sh` as the gate (memory safety).

**Open / deliberately out of scope (follow-up work):** `list<string>.insert`/`.set`/`.clear` have
the same latent pattern (not on the reported path — event_log uses push/remove). The
**kind 3 map scope drop** for LOCAL maps stays deregistered for now (the dense `aiml_map_free` is
correct now, but the move detection/ownership analysis for producer-returned map locals must be
audited first before the drop is armed) — member maps are freed via the model free
path, local producer maps (`spend_amorph → alloc`) are move-out.

**Lesson:** container ownership belongs IN the container (dup-on-insert), not in fragile caller-side
drop suppression. As soon as values are actually freed, "store raw + suppress the caller drop"
is only correct across a single scope.

# 11. Value model sharing double free class (drops-on-wide, 2026-09-13)

**Context:** after the container ownership fix (§10), mana/event log ran again, but playing
a creature/a spell crashed (`free(): double free`). The cause was NOT the container fix —
it is PRE-EXISTING and appeared because `AIML_LIFETIME_DROPS=1` (the deploy default of the game configs)
now actually frees models. spec/61 was "DEFAULT OFF", the re-enable gate "full suite green" never
met. At the user's request, the test suite now runs with drops ON by default
(`run_all_tests.sh` exports `AIML_LIFETIME_DROPS=1`), so that exactly this path is gated.

**Root cause (systemic):** value models with heap fields are copied SHALLOWLY in many places
(a struct copy shares the inline list/map `data` pointers). With drops ON, every
owner then frees the same buffer → double free. Affected escape routes (each located via valgrind+gdb
on the model `serNodes` + AIML function), all fixed, gated on drops-on:

1. **A producer returns a VIEW** (`return at(..)` / `return obj.field` / accessor) →
   deep copy into a temp before `ret` (visitRetStmt, structural view detection; no false alarm on
   owned-built locals such as `MatchResp out`).
2. **`set [local, view]`** (e.g. `load_entry_at` `set [out, {at:[lez,pos]}]`) → deep copy in
   visitSetStmt (AtExpr/member/accessor source).
3. **`decl [x, Model, view]` + a later `return x`** → view taint `Symbol::is_view_alias` at the
   decl (read-only views stay cheap, no copy), deep copy only at the return.
4. **`idx: [list, i, model]`** index store (e.g. `store_entry_at` `sez[pos]=ent`, everywhere in
   combat) → free-old + deep copy. **CRITICAL order:** the source can ALIAS the TARGET slot
   (write-back idiom `idx:[c,i,at(c,i)]`), so deep copy into an independent temp first,
   THEN free the old element, THEN memcpy the temp into the slot. Free-before-copy read freed
   memory in `enc()` → SIGSEGV (that was the second, deeper crash).
5. **cascade_class instance** (`new MkpEffPool`, spec/58) → EXCLUDED from the owned box drop
   (`!ctx.cascadeClasses.count(boxed)` in freshOwned + visitDeclStmt, analogous to
   `classGroupContainers`). Every instance BORROWS the once-loaded, shared cascade buffer
   (all `new` point to the same global rows) — a kind 4 drop would have freed the shared global
   → double free across instances.

**Result (drops-on):** previously crashing tests now green — `test_match_play`,
`test_creature_costs`, `test_creature_ability_reaction`, `test_target_soulmage_or_creature`,
`test_trigger_per_match`, `test_mkp_effect_cascade`, `test_mkp_multi_trigger`. The container sanitizer
suite stays green (compiler changes memory-safe).

**Debug technique (documented for reuse):** `MALLOC_CHECK_=3` + gdb breaks at the REAL
double free (not only later at `malloc_consolidate`); gdb symbolises JIT function names
(`fre`/`aiml_ser_free_decoded` in libaiml.so + the AIML function above) and `ns=__aiml_sernodes_X`
names the model. CAUTION: address reuse (free→alloc-same-addr→free) looks like a double free in pure
address logs — only the MALLOC_CHECK abort or valgrind is reliable.

**Open:** leak checking of the suite via `AIML_MEM_DIFF` (user request) — after the crashes are gone.
Perf: the escape deep copies only run at escape places (return/set-from-view/index store), NOT
on every read-only view access.

# 12. Drops-on suite hardening: per-route fixes (2026-09-13, continued)

At the user's request, the 9rooms suite now runs drops-on by default. It uncovered ~28 failures; ~18
of them were drops-on-SPECIFIC crashes (rc=139 SIGSEGV / rc=134 double free; all green with
drops-off) — the PRE-EXISTING, never-gated drops-on class. No root fix reached all of them at
once; there are SEVERAL independent roots. Debugging throughout via `MALLOC_CHECK_=3`+gdb
(symbolises JIT functions + `ns=__aiml_sernodes_X` names the model).

Fixed roots (all gated on drops-on):
- **Value model write-back class** (§11): `store_entry_at` self-alias index store (deep copy first,
  then free), producer-returns-view, cascade_class box drop exclusion. → 7 tests green
  (match_play, creature_costs, creature_ability_reaction, target_soulmage_or_creature,
  trigger_per_match, mkp_effect_cascade, mkp_multi_trigger).
- **Model string field default = IMMORTAL "" instead of a `.rodata` literal.** `initClassContainerMembers`
  set unassigned string fields to `CreateGlobalStringPtr("")` (rodata). aiml_ser_free_decoded
  decrefs EVERY string field on drop — the decref of a rodata literal reads its `-8` header
  (a page boundary fault OR an STRN magic false positive → `free()` of rodata → SIGSEGV). Fix: a new
  `aiml_str_empty()` returns a static, refcounted "" with the sentinel refcount
  `AIML_STR_IMMORTAL` (incref/decref no-op, never freed). `.len`/strlen still see "".
  → +5 tests green (add_life_clamp, buff_expiry, x_mana_cost, ability_text, cost_processor);
  enchantment_picker from crash → logic fail. **12 crash sites fixed in total.**

**Rejected (net negative):** full value semantics (deep copy of views at the decl). Broke
9rooms logic (enchantment_picker: the in-place view mutation `myc=at(..); apply_buff(myc)` loses the
buff) AND perf (match_play timeout: deep copy in hot loops). 9rooms code RELIES on shared
views with in-place mutation → real value semantics would require a game code rewrite. Therefore: views stay
SHARED, deep copy ONLY at real escape places (return/set/idx store).

**Remaining drops-on crashes (independent roots, open):**
- `typed_card_persist`: rodata "" in a CreatureCard string field WITH an explicit default
  (the `m.initValue` path in initClassContainerMembers, OR an embedded model field) — the immortal fix
  only covers `!m.initValue`. Fix direction: make string literal defaults immortal/dup too.
- `card_states`: a `list<Buff>`/`list<CardState>` element with a GARBAGE string (not rodata — a real
  garbage pointer) → an init/corruption root for list<Model> elements.
- `any_deck_draw`: C++ `__cxa_throw` → abort (an AIML exception, NOT a memory crash) — a separate cause.
