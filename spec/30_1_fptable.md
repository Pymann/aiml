# fptable — Function Pointer Table for Hot-Reload

## 1. Overview

The fptable approach replaces direct `call func` instructions with indirect calls
through a global pointer table `__aiml_fptable`. Hot-reload only updates one
pointer in the table — all existing call sites see the new implementation
on the next call, without being modified themselves.

```
Without fptable (AOT, direct):        With fptable (AOT, indirect):

call worker_task                      load rax, [__aiml_fptable + 0*8]
  → address fixed at link time          call rax
  → no hot-reload possible               → address swappable at runtime
```

---

## 2. Table Structure

The AOT compiler emits a global array variable with one `i8*` entry
per known function:

```llvm
; LLVM IR (AOT-generated):
@__aiml_fptable = global [3 x i8*] [
  i8* bitcast (i32 ()* @worker_task to i8*),   ; Index 0
  i8* bitcast (void (i8*)* @log_msg to i8*),   ; Index 1
  i8* bitcast (i32 (i32)* @compute to i8*)     ; Index 2
]

; Symbolic index constants (one per function):
@__aiml_idx_worker_task = constant i32 0
@__aiml_idx_log_msg     = constant i32 1
@__aiml_idx_compute     = constant i32 2
```

The table lives in the `.data` segment (readable and writable). Each entry is
exactly 8 bytes (pointer on x86-64) → atomically updatable (see section 5).

---

## 3. Call Emission in the AOT Compiler

Instead of `call i32 @worker_task()`, the compiler emits:

```llvm
; Indirect call through table pointer:
%slot    = getelementptr [3 x i8*], [3 x i8*]* @__aiml_fptable, i32 0, i32 0
%fp_raw  = load i8*, i8** %slot
%fp      = bitcast i8* %fp_raw to i32 ()*
%result  = call i32 %fp()
```

**Constraint**: The compiler must know the function index (`0`, `1`, ...) at
compile time. All definitions in a compilation unit are known → no problem.
External definitions (foreign modules) get an entry in an extended table
(section 6).

---

## 4. Hot-Reload: Updating Table Pointers

When a new version of `worker_task` has been compiled into a JIT buffer:

```cpp
// C side (exec_impl.cpp or hot-reload runtime):
void aiml_fptable_update(int idx, void* new_fn_ptr) {
    __atomic_store_n(
        &__aiml_fptable[idx],
        new_fn_ptr,
        __ATOMIC_SEQ_CST   // full memory barrier
    );
}

// Call:
uint64_t new_addr = jit_engine->getFunctionAddress("worker_task");
aiml_fptable_update(0, (void*)new_addr);
```

From the next `call rax` onwards, all callers load the new address.
**No mprotect, no call-site patching, no cache flush needed.**

---

## 5. Atomicity

A 64-bit pointer (8 bytes) on an 8-byte-aligned table slot is
**atomically readable and writable** on x86-64 — guaranteed by the architecture
(Intel SDM Vol. 3A §8.1.1: "The processor guarantees that a single QWORD memory
access is atomic if it is naturally aligned").

Requirement: `__aiml_fptable` is 8-byte aligned (default for global arrays in LLVM).

```
Thread A calls worker_task:           Thread B updates pointer:
  load rax, [fptable + 0*8]            store [fptable + 0*8], new_addr (atomic)
  call rax
```

Thread A sees either the old or the new pointer — never a half-written one.
**No mutex, no thread suspend needed.**

---

## 6. New Functions at Runtime

The table is emitted at compile time with a fixed size. Functions newly loaded
at runtime (from a code bundle) that did not previously exist have no table slot.

**Options:**

### 6.1 Reserved Slots

The AOT compiler reserves N empty slots at the end of the table:
```llvm
@__aiml_fptable = global [103 x i8*] [
  ; 3 known functions
  i8* bitcast (@worker_task to i8*),
  i8* bitcast (@log_msg     to i8*),
  i8* bitcast (@compute     to i8*),
  ; 100 empty slots for dynamic extension
  i8* null, i8* null, ..., i8* null
]
```

### 6.2 Dynamic Extension via Indirection

Instead of a direct array: a global pointer to a heap-allocated block,
reallocated on demand:
```cpp
void** __aiml_fptable_ptr = initial_table;
int    __aiml_fptable_size = 3;

// Extension (lock needed — rare path):
void aiml_fptable_grow(void* new_fn) {
    void** old = __aiml_fptable_ptr;
    void** neu = malloc((size + 1) * 8);
    memcpy(neu, old, size * 8);
    neu[size] = new_fn;
    __atomic_store_n(&__aiml_fptable_ptr, neu, __ATOMIC_SEQ_CST);
    size++;
    // do not free old immediately — may still be in use
}
```

For the initial implementation, **reserved slots** (section 6.1) are sufficient.

---

## 7. Performance

| Situation | direct call | fptable call |
|-----------|-------------|--------------|
| L1 cache hit | 1 ns | ~2 ns (+1 load) |
| L1 cache miss | 1 ns | ~4–10 ns (pointer in cache line) |
| Branch misprediction | rare | possible (indirect jump) |

The table is typically small → fits entirely in L1/L2. Practical overhead:
**1–2 ns per call** — negligible in most applications.

Critical inner loops: use `@inline` marking there → function is inlined,
no indirect call.

---

## 8. Comparison with Trampoline Patching

| Criterion | fptable | Trampoline |
|-----------|---------|------------|
| Compiler change | yes (indirect calls) | no (direct calls remain) |
| Call overhead | +1 load (always) | 0 (unpatched), +1 jmp (patched) |
| Atomicity | trivial (8-byte store) | complex (5-byte write) |
| Size limit | none | none (JIT buffer) |
| mprotect needed | no | yes |
| New functions | slot concept needed | no problem (new buffer) |
| `--rdynamic` needed | no (table is internal) | yes (for symbol lookup) |

**fptable** is simpler to implement and safer with respect to atomicity,
but always has a small call overhead.
**Trampoline** has zero overhead in the normal case, but requires mprotect and
more complex atomicity handling.

---

## 9. Implementation Plan

### Phase 1 — AOT Compiler Emits Table

**`src/core/ast_compiler.cpp` / `code_gen`:**
- After `registerTypes`: collect all known function names → assign indices
- Emit `@__aiml_fptable` as a global variable (+ N empty slots)
- `@__aiml_idx_<name>` as a global constant per function

### Phase 2 — Indirect Call Emission

**`src/core/ast_compiler_call.cpp`:**
- `visitCall`: instead of `builder.CreateCall(llvmFn, ...)` → GEP + load + indirectCall
- Only when `ctx.useFptable` is active (flag, default: false during transition)

### Phase 3 — Hot-Reload Runtime

**`exec_impl.cpp`:**
- `aiml_fptable_update(int idx, void* fn)` — atomic store
- `aiml_fptable_register(const char* name, void* fn)` — for new functions (empty slot)
- In `aiml_exec_code`: after JIT compilation → call `aiml_fptable_update`

### Phase 4 — Linker Flags

**`CMakeLists.txt` / Build:**
- AOT binary: `-Wl,--export-dynamic` for runtime access to table from `libaiml`

---

## 10. Known Limitations

- fptable must have a fixed size at compile time (or an extension concept is needed)
- Inline functions bypass the table → no hot-reload for inlined code
- Struct layout changes remain breaking changes (independent of fptable)
- First phase: `ctx.useFptable = false` → fptable not yet active
