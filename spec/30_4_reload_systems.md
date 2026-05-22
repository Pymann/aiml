# Hot-Reload Systems — Overall Architecture

This document summarizes all hot-reload mechanisms and shows how they
interact. Details on the individual mechanisms: `38_1_fptable.md`,
`38_2_trampoline_patching.md`, `38_3_symbolism.md`.

---

## 1. Three Independent Hot-Reload Problems

```
┌─────────────────────────────────────────────────────────────────┐
│  Problem 1: CODE                                                │
│  Functions / Methods — pure machine code                        │
│  Solution: fptable or Trampoline-Patching (38_1 / 38_2)        │
├─────────────────────────────────────────────────────────────────┤
│  Problem 2: TYPES                                               │
│  Classes / Models / Enums / Interfaces — schemas                │
│  Solution: Schema-Registry + Offset-Table                       │
├─────────────────────────────────────────────────────────────────┤
│  Problem 3: LIVE DATA                                           │
│  Heap instances with old layout                                 │
│  Solution: Member-Padding (Fast-Path) + Lazy-Tolerance (Fallback) │
└─────────────────────────────────────────────────────────────────┘
```

All three problems are orthogonal and have independent solutions.
Code hot-reload works without schema reload and vice versa.

---

## 2. Code Hot-Reload (Functions & Methods)

Summary from 38_1 and 38_2:

### 2.1 fptable (compile-time restructuring required, trivial atomicity)

```llvm
; Instead of a direct call:
call i32 @worker_task()

; Via table:
%fp = load i8*, i8** getelementptr(@__aiml_fptable, 0, <idx>)
call i32 %fp()
```

Hot-reload = atomic 8-byte pointer store. No mprotect.

### 2.2 Trampoline-Patching (no compile-time restructuring, zero overhead)

```
First byte of old function → E9 <rel32> (jmp to new implementation)
New implementation → JIT-Buffer (mmap near binary)
```

Hot-reload = mprotect + memcpy(5 bytes) + mprotect back.

### 2.3 Recommendation

| Scenario | Recommendation |
|----------|-----------|
| Single-Thread JIT | Trampoline (zero overhead, simple) |
| Multi-Thread AOT | fptable (trivial atomicity) |
| Performance-critical, no reload needed | `@noreload` → direct call stays |

---

## 3. Schema-Registry — Type Hot-Reload

### 3.1 Structure

The AOT compiler emits a global **Schema-Registry** alongside the binary
(in-memory, no disk I/O) and an **offset table** (analogous to fptable):

```
__aiml_schema["Payload"] = {
    current_version: 1,
    versions: {
        1: {size: 8,  fields: {id: 0, value: 4}},
    }
}

__aiml_offsets = [
    /* [0] Payload.id    */ 0,
    /* [1] Payload.value */ 4,
    /* [2] Status.Ok     */ 0,
    ...
]
```

### 3.2 Field Access via Offset Table

```llvm
; Instead of GEP with baked-in index:
%ptr = getelementptr %Payload, %Payload* %p, i32 0, i32 1   ; old: baked

; Via offset table (1 load, ~1 ns, L1-cache):
%off = load i64, i64* getelementptr(@__aiml_offsets, 0, 1)
%base = bitcast %Payload* %p to i8*
%ptr  = getelementptr i8, i8* %base, i64 %off
%v    = load i32, i32* %ptr
```

**Schema-Hoisting**: The compiler fetches offsets once in the function prologue into
local variables — inner loops without table access:

```llvm
entry:
  %off_value = load i64, i64* getelementptr(@__aiml_offsets, 0, 1)  ; 1× at entry
loop:
  %ptr = getelementptr i8, i8* %base, i64 %off_value   ; register — no load anymore
  %v   = load i32, i32* %ptr
  br loop
```

### 3.3 Hot-Reload of a Schema

```
New field appended (Payload.priority, offset 8):

1. Update schema registry:
   __aiml_schema["Payload"].versions[2] = {size: 12, fields: {id:0, value:4, priority:8}}
   __aiml_schema["Payload"].current_version = 2

2. Extend offset table:
   __aiml_offsets[3] = 8    ← new slot for Payload.priority

3. Register new slot index in compiler metadata
   (for future compilations that reference the field)
```

Existing functions that read `value` continue loading `__aiml_offsets[1] = 4`
→ correct. New functions that read `priority` load `__aiml_offsets[3] = 8`.

---

## 4. Live Data — The Heap Problem

The hardest problem: instances allocated before the schema hot-reload have
the old byte layout on the heap. Two mechanisms solve this together:

### 4.1 Member-Padding (Fast-Path — zero overhead)

Every struct is rounded up to the next power of two from the start
and zero-initialized (`calloc` or `mmap`):

```
Payload v1 (packed):   [id: 0-3][value: 4-7]          = 8 bytes
Payload v1 (padded):   [id: 0-3][value: 4-7][_: 8-15] = 16 bytes (next power of 2)
```

When `priority` (i32) is added as a third field:
- New offset: 8 — **falls within the already-allocated padding**
- Padding bytes were `calloc`-initialized → **priority = 0 by default**
- Old instances read `priority` → correctly 0 (sensible default)
- **No version check, no warning, no mprotect — zero overhead**

```
Padding strategy:
  packed size ≤  8 bytes  → allocate  16 bytes
  packed size ≤ 16 bytes  → allocate  32 bytes
  packed size ≤ 32 bytes  → allocate  64 bytes
  packed size ≤ 64 bytes  → allocate 128 bytes
  ...
  packed size >  N bytes  → allocate  2 × packed size
```

**When is padding not enough?** When after the hot-reload the total size of all
fields exceeds the original padding size. Example: Payload starts with
8 bytes packed → 16 bytes padded. After 3 hot-reloads with 4 bytes of new field each:
8 + 4 + 4 + 4 = 20 bytes > 16 bytes → padding exhausted → **fallback**.

### 4.2 Lazy-Tolerance with Schema-Version-Tag (Fallback)

When padding is exhausted, a new **major version** of the schema must be created.
New instances are allocated with the new size. Existing instances
receive a **schema-version-tag** in the allocation header:

```
Heap layout (with 4-byte header):
  [schema_version: u32][id: 4B][value: 4B][priority: 4B][...]
   ↑ hidden, 4 bytes overhead per instance
```

On every field access the runtime checks:

```
Reading Payload.value on instance p:
  p.schema_version  = 1  (old)
  current_version   = 2  (new, more space)

  → Look up "value" in version 1 of the schema history:
      v1.fields["value"].offset = 4  ✓ (exists, correct offset)
  → Read bytes 4-7 of the instance (correct)
  → [WARNING] "Payload.value: instance is schema v1, current v2
               — field still valid, but instance is outdated"
```

```
Reading Payload.new_field on instance p:
  p.schema_version  = 1  (predates new_field)
  "new_field" in v1 → not found

  → [WARNING] "Payload.new_field: field does not exist in schema v1
               — default value 0 returned"
  → Returns 0 (no crash)
```

**Key property**: Access to fields that existed in the instance's version
is **always correct** (append-only guarantees identical offsets).
Only access to new fields returns 0 + warning. The instance lives out its
natural lifecycle and will be freed eventually — no forced migration.

### 4.3 Compatibility of Both Mechanisms

```
New field added
        │
        ▼
Fits in padding?
   ├── Yes → read zeros, no warning, no tag    ← Fast-Path (common)
   └── No  → Major-version bump
                  │
                  ▼
           New instances: larger allocation, version-tag = 2
           Old instances: version-tag = 1, keep running
                  │
                  ▼
           Field access on old instance:
             Field existed in v1 → correct + WARNING
             Field new in v2     → 0 + WARNING
                  │
                  ▼
           Instance goes out-of-scope → no more warnings
```

---

## 5. Enums — Special Case

Enum values are currently compile-time immediates (baked in). Hot-reload is
**not possible** without a lookup table (analogous to `__aiml_offsets`):

```llvm
; Currently (not reloadable):
%cmp = icmp eq i32 %s, 1   ; 1 = Error, baked

; With enum lookup table (future):
%ev  = load i32, i32* getelementptr(@__aiml_enum_Status, 0, 1)  ; 1 = Error-index
%cmp = icmp eq i32 %s, %ev
```

**Safe enum changes** (without lookup table):
- Adding a new member at the end → old code doesn't know it, no problem
- Changing the value of an existing member → breaking (immediates wrong)

**Schema-version-tag for enums**: Enum values are stored as `i32`.
An instance tag doesn't help here — there is no "instance", only the value.
Therefore: never change enum values, only extend them.

---

## 6. Performance Model

### Per Field Access (Runtime Overhead)

| Mechanism | Overhead | Condition |
|-------------|---------|-----------|
| Baked GEP (today) | **0 ns** | no hot-reload |
| Offset table, L1-hit | ~1 ns | schema in table |
| Schema-Hoisting | ~0 ns (amortized) | offset once per function |
| Version-check (padding ok) | **0 ns** | fast-path: no tag needed |
| Version-check (tag present) | ~2–5 ns | comparison + schema lookup |
| Warning emission | ~50–200 ns | only on outdated instances |

### Memory Overhead

| Mechanism | Overhead |
|-------------|---------|
| Member-Padding | 1× packed size (16B instead of 8B, 64B instead of 48B, ...) |
| Schema-Version-Tag | 4 bytes per instance (only on major-version bump) |
| Schema-Registry | ~1 KB per type (name, fields, version history) |
| Offset table | 8 bytes per field × N fields (typically < 10 KB total) |

### `@noreload` Annotation

For performance-critical types: disable padding, keep GEP baked:

```aiml
- class:
    name: Vec3
    noreload: true        # no padding, no version-tag, no schema entry
    members:
      - [x, f32]          # offset 0 — forever
      - [y, f32]          # offset 4 — forever
      - [z, f32]          # offset 8 — forever
```

---

## 7. Full Picture: What Applies When

```
sys.exec receives code bundle
        │
        ├── code_chunks (functions/methods):
        │       version > ctx.version?
        │         ├── Yes → Trampoline/fptable hot-reload
        │         └── No  → SKIP
        │
        ├── code_chunks (classes/models):
        │       version > ctx.version?
        │         ├── Yes, Append-Only → update schema-registry + offset table
        │         │       Padding sufficient?
        │         │         ├── Yes → new fields in existing instances = 0
        │         │         └── No  → major-version bump, activate version-tags
        │         ├── Yes, Breaking Change → ERROR (not supported)
        │         └── No  → SKIP
        │
        └── data_chunks (instance initializations):
                Execution → new instances allocated with current schema
```

---

## 8. Implementation Order (Phases)

### Phase 1 — Code Hot-Reload (Trampoline, Single-Thread)
Prerequisite: sys.exec (already implemented, phase 2/3 from 29_2)
- JIT-Buffer via `mmap`
- `write_trampoline(old_func, new_addr)`
- Version-check in `aiml_exec_code` → trampoline instead of SKIP

### Phase 2 — Member-Padding
- AOT compiler: round struct allocation to next power of two
- `calloc` instead of `malloc` for all struct allocations (zeros guaranteed)
- Store padding size in schema registry

### Phase 3 — Schema-Registry + Offset Table
- Emit `__aiml_offsets` array
- Field accesses via offset table (with schema-hoisting in prologue)
- Initialize schema registry (in-memory, at program start)

### Phase 4 — Lazy-Tolerance (Version-Tags)
- Allocation header with `schema_version: u32`
- Field-access runtime: check version, select correct offset version, emit warning
- `sys.free_version` for schema-history cleanup

### Phase 5 — fptable (Multi-Thread)
- Emit `__aiml_fptable`
- Indirect call emission in AOT compiler
- `aiml_fptable_update` for atomic updates

---

## 9. Persistence of Hot-Reload State

### 9.1 The Problem

All hot-reload changes (trampoline JIT-buffers, fptable entries,
schema-registry updates) live in RAM. They are lost when the process exits —
especially critical when the code objects came over the network
and are not available again.

### 9.2 Hot-Reload Journal (Write-Ahead Log)

Every `sys.exec` that triggers a hot-reload writes the `code_chunks`
to a journal before applying. The content is YAML — already contained in the code object,
no extra effort.

```
sys.exec(pkg)
    │
    ├── 1. Write journal entry:
    │        hot_reload.journal ←
    │          {base_hash: "sha256:4a7f...",
    │           bundle_id: "abc123",
    │           timestamp: "2026-02-28T14:30:00Z",
    │           code_chunks: ["- def:\n    name: worker_task\n    version: 2\n..."]}
    │
    └── 2. Apply hot-reload (trampoline / schema update)
```

On the next program start:

```
Binary starts
    │
    ▼
Journal present + base_hash matches?
    ├── No  → start normally
    └── Yes → entries replayed in order:
                re-compile code_chunks
                re-apply trampoline / fptable
                rebuild schema registry
                → full state reconstructed
```

### 9.3 JIT Mode: Journal = more AIML at the end of the source file

In JIT mode (`aiml -r main.aiml`) there is no binary. The journal is appended
directly to the source file — AIML's version check handles deduplication
automatically on the next compiler run:

```aiml
# main.aiml — original
- def:
    name: worker_task
    version: 1
    body:
      - print: ["v1"]

# === HOT-RELOAD JOURNAL — 2026-02-28T14:30:00Z (bundle: abc123) ===
- def:
    name: worker_task
    version: 2          # higher version → overwrites v1 automatically
    body:
      - print: ["v2"]
```

No separate replay mechanism needed — the compiler sees both definitions,
version check applies, v2 wins.

### 9.4 Snapshot: Journal → New Baseline

The journal grows over time. `sys.snapshot` compresses: all entries
are merged into the source code, recompiled, journal cleared.

```aiml
- call: [sys.snapshot, "program_v2.aiml"]   # source file with all updates merged
- call: [sys.snapshot]                       # overwrites current source file
```

```
Base-Source + Journal → merge → program_v2.aiml → recompile
                                                 → program_v2 (no journal needed)
```

Analogous to `git commit` — accumulated patches become the new baseline.

### 9.5 Base Conflict Detection

When the binary/source file itself is updated (new deployment),
the journal could be applied against an incompatible base.
The journal contains a `base_hash`:

```json
{
  "base_hash": "sha256:4a7f...",
  "entries": [...]
}
```

If `base_hash` does not match at startup → discard journal + warning.
The user decides on manual porting or discards the journal state.

### 9.6 Persistence Modes

| Mechanism | When active | Suitability |
|---|---|---|
| Auto-journal (default) | on every hot-reload `sys.exec` | always recoverable |
| `sys.checkpoint()` | explicitly in code | controlled persistence |
| `sys.snapshot(path)` | explicit, merges + compiles | release artifacts |
| SIGTERM handler | on clean shutdown | fallback for unplanned stops |
| `pkg.persist = false` | opt-out per bundle | ephemeral / temporary patches |

---

## 10. Known Limitations (All Phases)

- Breaking changes (field reorder, type change, field removal): never supported → error
- Enum value changes: only supported from lookup table onward (phase 3+)
- Inlined functions (`@noreload`): no code hot-reload
- `@noreload` types: no schema reload, no padding overhead
- AOT multi-thread without fptable: trampoline atomicity only safe for single-thread
- Struct size > padding limit: major-version bump needed (rare, but possible)
- Warning flood: many outdated instances → many warnings (throttling recommended)
