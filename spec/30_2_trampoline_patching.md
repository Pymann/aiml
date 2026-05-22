# Trampoline Patching — In-Place Hot-Reload Without Call-Site Changes

## 1. Overview

Trampoline patching overwrites the first bytes of an existing function with
a jump (`jmp`) to newly compiled code. All `call old_func` instructions
remain unchanged — on the next call they land in the new code, because the
entry point has been redirected.

```
Before patch:                         After patch:
0x401000  push rbp      <- entry      0x401000  jmp 0x7f3c110000  <- 5 bytes
0x401001  mov rbp, rsp               0x401005  (overwritten)
0x401003  ...                        0x401007  ...   (never reached)
0x40102F  ret                        0x40102F  ret

                                      0x7f3c110000:  <- new function in JIT buffer
                                        push rbp
                                        mov rbp, rsp
                                        ...
                                        ret
```

**No compiler rework needed** — direct call instructions remain direct.
Overhead in the unpatched state is zero.

---

## 2. Jump Stubs

### 2.1 Relative 5-Byte Jump (Standard)

```
E9 <rel32>
```

`rel32 = new_addr - (old_func + 5)` — offset relative to the end of the jump instruction.

Range: ±2 GB from the jump instruction. Sufficient when the JIT buffer is allocated
near the binary (Section 4).

```c
int32_t rel32 = (int32_t)((intptr_t)new_addr - ((intptr_t)old_func + 5));
uint8_t stub[5] = {
    0xE9,
    (rel32 >>  0) & 0xFF,
    (rel32 >>  8) & 0xFF,
    (rel32 >> 16) & 0xFF,
    (rel32 >> 24) & 0xFF,
};
```

### 2.2 Absolute 14-Byte Jump (Fallback)

When `|new_addr - old_func| > 2 GB` (e.g. for very large binaries or
distant mmap regions):

```
FF 25 00 00 00 00   ; jmp [rip+0]  — reads 8 bytes directly after the instruction
<8 bytes absolute target address, little-endian>
```

```c
uint64_t abs_addr = (uint64_t)new_addr;
uint8_t stub[14] = {
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,   // jmp [rip+0]
    (abs_addr >>  0) & 0xFF,
    (abs_addr >>  8) & 0xFF,
    (abs_addr >> 16) & 0xFF,
    (abs_addr >> 24) & 0xFF,
    (abs_addr >> 32) & 0xFF,
    (abs_addr >> 40) & 0xFF,
    (abs_addr >> 48) & 0xFF,
    (abs_addr >> 56) & 0xFF,
};
```

Prerequisite: the old function has at least 14 bytes. In practice always true
(function prologue + first instruction = at least 3–7 bytes; with NOP padding >= 14).

---

## 3. Making Code Pages Writable (mprotect)

Machine code resides in `.text` pages with `r-x` (Read + Execute, no Write).
Before patching, the affected page must be temporarily made writable:

```c
#include <sys/mman.h>
#include <unistd.h>

void patch_function(void* old_func, const uint8_t* stub, size_t stub_len) {
    long   page_size = sysconf(_SC_PAGESIZE);              // typically 4096
    void*  page      = (void*)((uintptr_t)old_func & ~(page_size - 1));

    // 1. Make writable
    mprotect(page, page_size, PROT_READ | PROT_WRITE | PROT_EXEC);

    // 2. Write stub
    memcpy(old_func, stub, stub_len);

    // 3. Restore protection
    mprotect(page, page_size, PROT_READ | PROT_EXEC);

    // 4. Invalidate instruction cache (x86-64: memory barrier is sufficient)
    __builtin___clear_cache(old_func, (char*)old_func + stub_len);
}
```

**PROT_WRITE + PROT_EXEC simultaneously** may be forbidden on modern systems
with a W^X policy (e.g. SELinux, OpenBSD). Safe alternative:
Write -> `mprotect(PROT_READ|PROT_WRITE)`, then `mprotect(PROT_READ|PROT_EXEC)`.
The brief intermediate phase without Execute is non-critical on a single thread.

---

## 4. JIT Buffer Allocation

New code must reside in an executable memory region. For the ±2 GB range
(5-byte jump), the buffer should be allocated **near the binary**:

```c
// mmap with address hint near the binary:
void* alloc_near(void* hint, size_t size) {
    // Binary address as hint -> kernel tries to allocate nearby
    void* buf = mmap(
        (void*)((uintptr_t)hint & ~0xFFFFF),  // 1 MB below as hint
        size,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1, 0
    );
    if (buf == MAP_FAILED) return nullptr;
    return buf;
}
```

For AIML: a static JIT buffer (e.g. 4 MB) allocated on the first hot-reload call,
then filled sequentially (bump allocator). Old versions remain in the buffer
(no free — until `sys.free_version` is called, Section 7).

---

## 5. Variant: Pre-Padded Buffers

As described in `38_hot_reload.md`: the AOT compiler emits each function
with NOP padding up to a minimum size (e.g. 64 or 256 bytes):

```asm
worker_task:
    push rbp
    mov rbp, rsp
    ...
    ret
    nop; nop; nop; ...    <- padding to 64 bytes
.Lworker_task_end:
```

**Advantage**: New code <= padding size -> direct in-place overwrite, no
JIT buffer, no external pointer needed.

**Disadvantage**:
- Padding size is arbitrary; functions grow and still need trampolines
- Binary becomes larger (every function at least N bytes)
- No real gain over pure trampolines, which always work

**Recommendation**: Padding only for the first 14 bytes (enough for the absolute jump),
not for the entire function body. This ensures the trampoline stub is always
writable without destroying the function itself.

---

## 6. Backup of Old Code

So that running threads can safely finish executing the old code:

```
Before patching:
  1. Compile new code into JIT buffer (target address known)
  2. Copy old code (first N bytes) to a separate buffer (backup)
     -> running threads currently in the prologue read from the original;
        after the patch they jump to the new code
  3. Write stub (5 or 14 bytes)
```

The backup is needed for `sys.free_version`: only once it is guaranteed
that no thread is still executing the old code may the backup buffer be freed.

For single-threaded AIML programs no backup is needed.

---

## 7. Memory Reclamation (`sys.free_version`)

```aiml
# Free old version of worker_task (version 1):
- call: [sys.free_version, "worker_task", 1]
```

In `exec_impl.cpp` / Runtime:
- Mark JIT buffer region of the old version as free
- Release backup buffer
- Clean up `ctx.functions["worker_task"].old_versions`

**Safety**: Only call when it is guaranteed that no thread is still executing
the old version (quiescence point, e.g. after barrier or join).

---

## 8. Atomicity

### The Problem

A 5-byte write is **not atomic**:

```
Thread A: reads bytes 0-3 of the instruction (old push rbp)
Thread B: writes bytes 0-4 (E9 xx xx xx xx)
Thread A: reads byte 4 — reads already new byte -> invalid instruction
```

### Solution 1: Single-Thread (simplest solution)

For AIML programs without parallelism: no problem. `sys.exec` is coordinated
between threads (channel sync), not called during a function.

### Solution 2: 2-Byte Patch with Prepared Function

Microsoft Windows hot-patching uses a 2-byte jump (`EB <rel8>`, range
±127 bytes) as an atomic first step to a trampoline in the NOP padding:

```
AOT-emitted function (prepared layout):
  0x401000: EB 09            ; jmp +9 (to the 14-byte stub region)  <- 2 bytes atomically patchable
  0x401002: nop nop nop nop  ; 4 bytes padding
  0x401006: nop nop nop nop  ; 4 bytes padding (14-byte stub fits from here)
  0x40100E: push rbp          ; actual function body
  ...

Patch process:
  1. Write absolute 14-byte stub into 0x401002-0x40100F (harmless — never reached)
  2. Atomically rewrite 2-byte `EB 09` to `EB 00` (or corresponding offset)
     -> now the function jumps to the stub
```

A 16-bit write (`mov word ptr [addr], val`) is atomic on x86-64 when 2-byte aligned.

### Solution 3: Thread Suspend (maximum safety)

```c
// Briefly suspend all threads (platform-specific):
// Linux: SIGSTOP all threads, patch, SIGCONT
// Or: pthread_barrier_wait at defined checkpoint locations
```

Complex, but correct for multi-thread + arbitrary functions.

---

## 9. Instruction Cache on x86-64

x86-64 has **cache-coherent** I-caches (MESI protocol): a `mov [addr], val`
automatically invalidates the I-cache line on all cores. Explicit cache flush
(`CLFLUSH`) or `__builtin___clear_cache` are technically not needed, but
recommended as portable code practice (ARM requires it, for example).

---

## 10. Comparison with fptable

See `38_1_fptable.md` §8 for the full table. Summary:

| | fptable | Trampoline |
|---|---|---|
| Compile time | emit indirect calls | no change |
| Runtime overhead (unpatched) | +1 load per call | **zero** |
| Runtime overhead (patched) | +1 load per call | +1 jmp |
| Atomicity | trivial (8-byte store) | complex |
| mprotect | no | yes |

**Recommendation**: For AIML in single-thread JIT mode -> Trampoline (zero overhead,
simple atomicity). For multi-thread AOT with frequent hot-reload -> fptable.

---

## 11. Implementation Plan

### Phase 1 — JIT Buffer

**`exec_impl.cpp`:**
- `static void* g_jit_buffer = nullptr` — allocated via `alloc_near` on first call
- `static size_t g_jit_offset = 0` — bump allocator
- `void* jit_alloc(size_t n)` — returns pointer, increments offset

### Phase 2 — Patch Function

**`exec_impl.cpp`:**
- `bool write_trampoline(void* target, void* dest)`:
  - Check range (±2 GB -> 5-byte, else -> 14-byte)
  - `mprotect` -> `memcpy` -> `mprotect` -> `__builtin___clear_cache`
  - Returns `true` on success

### Phase 3 — Integration into `aiml_exec_code`

On hot-reload (bundle_version > current_version):
```
1. Compile new code into JIT buffer -> new_addr
2. Save backup of old_func (first 14 bytes)
3. write_trampoline(old_func, new_addr)
4. Update ctx.functions[name].version
5. ctx.functions[name].func = new llvm::Function* (for metadata)
```

### Phase 4 — `sys.free_version`

**`ast_compiler_call.cpp`:**
- Intercept `sys.free_version` -> call `aiml_free_version(const char* name, int version)`
- In runtime: mark JIT buffer region as reusable

---

## 12. Known Limitations

- Multi-thread atomicity: initial implementation only correct for single-thread
- Functions < 5 bytes: theoretically possible (e.g. naked `ret` wrappers) -> need pre-padding
- W^X systems (OpenBSD, hardened Linux): `PROT_WRITE|PROT_EXEC` forbidden -> alternative mprotect flow needed
- ARM: explicit I-cache flush via `__builtin___clear_cache` mandatory
- Inlined functions: no patching possible (code is embedded in the caller)
- Struct layout changes: remain a breaking change (Section 38_3)
