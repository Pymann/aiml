# Symbol Semantics in Hot-Reload

## 1. The Core Problem: Three Layers of Symbol Meaning

A symbol (function, class, enum, interface, model) has three independent
layers in AIML that must be treated differently during hot-reload:

```
┌─────────────────────────────────────────────────────────┐
│ 1. Symbol Layer (Compile-Time)                          │
│    ctx.functions["f"] / ctx.classes["C"] / ctx.enums["E"]│
│    -> always immediately updatable                      │
├─────────────────────────────────────────────────────────┤
│ 2. Code Layer (Machine Code)                            │
│    Offsets, jump addresses, immediates — baked in       │
│    -> only replaceable via fptable or trampoline        │
├─────────────────────────────────────────────────────────┤
│ 3. Data Layer (Heap)                                    │
│    Live instances with current byte layout              │
│    -> never retroactively patchable without migration   │
└─────────────────────────────────────────────────────────┘
```

**The central question**: "Do symbols simply get a new meaning?"

Answer: **Layer 1 yes, always.** Layers 2 and 3 depend on the symbol type
and nature of the change — that is the core of this specification.

---

## 2. Functions — Fully Replaceable

Functions have **no persistent data** (no heap state). Their symbol
is purely code.

```
Change:                            Compatible?
─────────────────────────────────────────────
Implementation (body)              ✓ always
Return value type                  ✗ Breaking (call sites expect different type)
Signature (arguments)              ✗ Breaking (call sites become invalid)
Name                               ✗ Breaking (all callers must be updated)
```

**New meaning** (Layer 1 + 2): Symbol entry is updated, machine code
replaced via fptable/trampoline. All future calls invoke the new implementation.
Running calls finish cleanly with the old one (backup, Section 38_2 §6).

**Versioning**: the `version` field in the `def` block controls whether hot-reload occurs:
```aiml
- def:
    name: worker_task
    version: 2          # version 1 was in the main program
    body: ...
```
-> `bundle_version (2) > ctx.version (1)` -> Reload. Same or older version -> SKIP.

---

## 3. Enums — Baked-in Immediates vs. Lookup Table

### 3.1 Current Status: Compile-Time Constants

Enum values are embedded as LLVM immediates at compile time:

```aiml
- enum:
    name: Status
    values: [Ok, Error, Pending]
    # Ok=0, Error=1, Pending=2
```

```llvm
; Generated IR comparison:
%cmp = icmp eq i32 %s, 1   ; "1" is baked in — Error=1 fixed at compile time
```

**Consequence**: Enum values cannot be hot-reloaded. Changes to
enum values are **always breaking** — old code compares against wrong immediates.

### 3.2 Safe Changes

```
Change:                            Compatible?
─────────────────────────────────────────────
Add new member at the end          ✓ existing code doesn't know it, ignores it
Change existing values             ✗ Breaking (all icmp immediate comparisons wrong)
Remove member                      ✗ Breaking (code references invalid value)
Rename member                      ✗ Breaking (compile error in dependencies)
Change order                       ✗ Breaking (implicit values shift)
```

### 3.3 Future: Enum Lookup Table

Analogous to `__aiml_fptable`, a global enum value table could be emitted:

```llvm
@__aiml_enum_Status = global [3 x i32] [i32 0, i32 1, i32 2]

; Comparison via table:
%slot = getelementptr [3 x i32], [3 x i32]* @__aiml_enum_Status, i32 0, i32 1
%val  = load i32, i32* %slot
%cmp  = icmp eq i32 %s, %val
```

Then hot-reloading values (not the order!) would be possible. Performance cost:
+1 load per comparison. Deferred for the initial implementation.

---

## 4. Classes — The Code/Layout Separation Principle

Classes have two completely different aspects in hot-reload:

### 4.1 Methods (Code)

Methods are functions — fully replaceable as described in Section 2.
A class method `Foo.doThing` is `@Foo__doThing(i8* self, ...)` at the IR level.
fptable or trampoline replaces the code; the `self` pointer remains valid.

### 4.2 Fields (Layout)

Field offsets are embedded as GEP constants at compile time:

```aiml
- class:
    name: Payload
    members:
      - [id,    i32]   # Offset 0
      - [value, i32]   # Offset 4
```

```llvm
; Access to p.value:
%p_value = getelementptr %Payload, %Payload* %p, i32 0, i32 1   ; Index 1 = Offset 4
%v = load i32, i32* %p_value
```

If `version 2` now inserts a new field **before** `value`:
```aiml
- class:
    name: Payload
    version: 2
    members:
      - [id,       i32]   # Offset 0
      - [priority, i32]   # Offset 4  <- new (shifts value!)
      - [value,    i32]   # Offset 8  <- was 4, now 8
```

Old code reads `Index 1` -> reads `priority` instead of `value` -> **silent data corruption**.
No error message, no crash (at least on x86-64) — just wrong results.

### 4.3 Compatibility Matrix for Classes

```
Change:                                Methods   Layout    Compatible?
─────────────────────────────────────────────────────────────────────
Change method implementation           new       same      ✓ always
Add new field at the END               same      extended  ✓ old offsets remain
Insert new field in the MIDDLE         same      breaks    ✗ Breaking
Remove field                           same      breaks    ✗ Breaking
Change field type (same size)          same      possibly  △ dangerous
Change field type (different size)     same      breaks    ✗ Breaking
Rename class                           —         —         ✗ Breaking
```

**Golden rule**: New fields only at the end. Methods can be changed freely.

---

## 5. Models — Pure Data Schemas

Models (see `15_models.md`) are classes without methods — only fields and validation.
The layout problem from Section 4.2 applies identically.

**Additional dimension**: Models are serialized/deserialized. A hot-reloaded
model with a changed schema may fail when deserializing old JSON/YAML blobs.

```
Change:                            Compatible?   Serialization
───────────────────────────────────────────────────────────────
New optional field (with default)  ✓ Layout ok   ✓ old JSON lacks it -> default
New required field                 ✗ Layout breaks ✗ old JSON missing field
Remove field                       ✗ Layout breaks △ old JSON has extra field -> ignored
Change field type                  ✗ Layout breaks ✗ parse error possible
```

**Recommendation**: Use model versioning explicitly with the `version` field and
provide migration functions (Section 6.3).

---

## 6. Live Instances — The Migration Problem

### 6.1 The Problem

When class/model `Payload` is hot-reloaded (layout change), there may be
`Payload` instances on the heap that were allocated with the old layout:

```
Heap before hot-reload:       Heap after hot-reload:
  p1: [id=1][value=42]          p1: [id=1][value=42]   <- unchanged on heap
  p2: [id=2][value=7]           p2: [id=2][value=7]    <- unchanged on heap

New layout expects: [id][priority][value]
Old code reads p1.value at offset 4 -> 42 ✓
New code reads p1.value at offset 8 -> reads p2.id (=2) ✗ <- data corruption
```

### 6.2 Strategy 1: Invalidation (simplest solution)

Mark all live instances as invalid after a layout change.
Accessing them -> runtime error (exception or assertion).

Prerequisite: GC or explicit tracking of all heap allocations by type.
Not currently implemented in AIML (no GC).

### 6.3 Strategy 2: Migration (most correct solution)

A migration function is called during hot-reload:

```aiml
# Migration callback (optional, defined by the user):
- def:
    name: migrate_Payload_v1_to_v2
    args: [[old, Payload], [new, Payload]]   # old: Layout v1, new: Layout v2
    body:
      - set: [new.id,       old.id]
      - set: [new.priority, 0]         # default for new field
      - set: [new.value,    old.value]
```

This requires an instance registry (all `Payload*` pointers known) and is
too complex for the initial implementation.

### 6.4 Strategy 3: Append-Only (most pragmatic solution, recommended)

Only allow extensions at the end. Old code reads old fields correctly.
New code can read new fields. No migration needed.

```
Payload v1: [id: 0..3][value: 4..7]          <- 8 bytes
Payload v2: [id: 0..3][value: 4..7][prio: 8..11] <- 12 bytes, v1 fields unchanged
```

New code allocates 12 bytes. Old code only accesses bytes 0..7 — correct.
New fields in old instances are undefined (zero-initialized at malloc).

**Limitation**: Old code that knows `sizeof(Payload)` allocates too little.
In AIML: allocation always via `CodeImpl::new(ctx, typename)` -> uses current
`sizeof` -> no problem, as long as old code does not allocate Payload itself.

---

## 7. Interfaces — Structural Contracts

AIML interfaces are compile-time constructs (no vtable object on the heap).
A class "implements" an interface through structural conformance.

```aiml
- interface:
    name: Runnable
    methods: [[run, [], void]]

- class:
    name: Worker
    implements: [Runnable]
    methods:
      - def:
          name: run
          body: ...
```

**What happens when an interface is hot-reloaded?**

The interface itself has no heap object — it is only a type annotation. A
hot-reload means the interface constraint changes in the symbol table.

```
Change to interface:                    Consequence
────────────────────────────────────────────────────────────────────
Add new method                          All implementing classes become
                                        "incomplete" -> compile error in
                                        new code that uses the interface
Remove method                           Implementing classes have extra method
                                        -> harmless (structurally compatible)
Change method signature                 All implementations become incompatible
Rename interface                        All references must be updated
```

**New meaning?**: Yes, for newly compiled code immediately. Old compiled code
that already checked the interface constraint is unaffected (constraint
was validated at compile time, no runtime check).

**Hot-reloading an interface** practically always means hot-reloading all
implementing classes as well. These are captured in the bundle dependency graph
(sys.bundle includes dependencies transitively: referenced types AND called user
functions — see spec/29_2 §16; builtins stay out, they live in the runtime library).

---

## 8. Compatibility Overview

| Symbol Type | Code Change | Layout Change | Value Change |
|------------|--------------|-----------------|---------------|
| Function | ✓ (Trampoline/fptable) | — | — |
| Enum | — | — | ✗ (Immediates baked) |
| Enum (future, lookup table) | — | — | ✓ (if no reorder) |
| Class — Method | ✓ | — | — |
| Class — Fields | — | △ append-only only | — |
| Model — Fields | — | △ append-only only | — |
| Interface | △ (remove only) | — | — |

Legend: ✓ compatible, △ restricted, ✗ breaking change

---

## 9. Versioning Strategy (Recommendation)

```aiml
# Rule: always set version explicitly for hot-reload-capable definitions.

- class:
    name: Payload
    version: 1
    members:
      - [id,    i32]
      - [value, i32]

# Compatible update (append-only -> increment version):
- class:
    name: Payload
    version: 2
    members:
      - [id,       i32]
      - [value,    i32]
      - [priority, i32]   # NEW — at the end

# Incompatible update -> choose a new name instead of hot-reload:
- class:
    name: PayloadV3           # <- new name, no breaking hot-reload
    version: 1
    members:
      - [priority, i32]       # order changed
      - [id,       i32]
      - [value,    i32]
```

**Rule of thumb**:
- Code (functions, methods): hot-reload always possible -> increment `version`
- Data (classes, models): hot-reload only append-only -> increment `version`
- Enum values: no hot-reload -> wait for lookup table
- Incompatible schema changes: assign a new type name, write migration explicitly
