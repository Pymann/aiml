# OS Support & Bare-Metal Compilation

## What does "writing an operating system in AIML" mean?

An operating system (OS) is not a normal program. A normal program runs *on top of* an
operating system: it has system calls (syscalls), a C standard library (libc), a heap
allocator, stack setup code (`crt0`) — all provided for free. An operating system, on
the other hand, must run as the first program after the boot loader, without any
support underneath it. There is no libc. There is no `printf`. There is no heap. There
are no interrupts except those you set up yourself.

AIML can switch into this **freestanding mode**: the runtime library (`libaimlrt`) is
replaced by a minimal bare-metal runtime, LLVM generates bare-metal machine code for the
target architecture, and the linker receives a custom linker script. This allows writing
everything from a simple bootloader to a full microkernel.

---

## Compilation Targets

AIML recognizes two fundamentally different execution targets:

### Hosted Target (Default)
```yaml
# Default: runs as a process under an OS (Linux/macOS/Windows)
- target: hosted          # default; libc + libaimlrt available
```

### Freestanding Target (Bare-Metal)
```yaml
# Bare-Metal: no OS underneath
- target: freestanding
  arch: x86_64             # or: aarch64, riscv64, arm, i686
  format: elf              # or: pe (UEFI), raw (flat binary)
  entry: kernel_main       # entry point; no libc _start
```

In freestanding mode:
- No `import io` → no printf, no filesystem
- No `import net` → no socket
- No GC / no heap allocator (bring your own)
- No thread scheduling (bring your own)
- Available: `hw` (hardware namespace), `mem.phys`, `cpu`, `intr`

---

## Hardware Access

### What is MMIO?

**Memory-Mapped I/O (MMIO)** is the mechanism through which a processor communicates with
hardware peripherals. The hardware (e.g. a UART chip, a GPIO controller, a PCIe device)
*maps* its registers into the physical address space. This means: when the processor
writes to address `0xFFFF_C000`, the bits don't land in RAM but directly in a register of
the peripheral device. Conversely, a read from that address returns the current state of
the device.

On x86 there is additionally **port I/O**: a separate address space (16-bit, 65536 ports),
accessible with the special CPU instructions `IN` and `OUT`. On ARM and RISC-V everything
is MMIO (no separate port address space).

```yaml
# hw.mmio — Memory-Mapped Register Read/Write
# hw.port — x86 Port I/O (IN/OUT instructions)

# Example: UART0 on an ARM board (e.g. QEMU virt, address 0x09000000)
- decl: [uart_base, u64]
- set: [uart_base, 0x09000000]

# Write a byte ('H') to the UART Data Register
- call: [hw.mmio.write8, uart_base, 0x48]       # 'H' = 0x48

# Read the status register (offset +5 = Line Status Register)
- decl: [lsr, u8]
- set: [lsr, { call: [hw.mmio.read8, uart_base, 5] }]

# x86: write to COM1 (port 0x3F8)
- call: [hw.port.out8, 0x3F8, 0x48]             # 'H'
- decl: [status, u8]
- set: [status, { call: [hw.port.in8, 0x3FD] }] # Line Status Register
```

**MMIO API:**
```yaml
hw.mmio.read8(addr: u64) -> u8          # Read 1 byte from physical address
hw.mmio.read16(addr: u64) -> u16        # Read 2 bytes (LE)
hw.mmio.read32(addr: u64) -> u32        # Read 4 bytes (LE)
hw.mmio.read64(addr: u64) -> u64        # Read 8 bytes (LE)
hw.mmio.write8(addr: u64, val: u8)
hw.mmio.write16(addr: u64, val: u16)
hw.mmio.write32(addr: u64, val: u32)
hw.mmio.write64(addr: u64, val: u64)
hw.mmio.fence()                         # Memory barrier (DMB on ARM, MFENCE on x86)
```

**Port I/O (x86-only):**
```yaml
hw.port.in8(port: u16) -> u8
hw.port.in16(port: u16) -> u16
hw.port.in32(port: u16) -> u32
hw.port.out8(port: u16, val: u8)
hw.port.out16(port: u16, val: u16)
hw.port.out32(port: u16, val: u32)
```

---

## Interrupts & Exceptions

### What are interrupts?

The processor normally executes instructions sequentially. An **interrupt** is a signal
to the CPU: "pause briefly, handle this event, then continue." There are three kinds:

1. **Hardware interrupts**: External devices signal (timer tick, keyboard input,
   network packet arrived). The interrupt controller (APIC on x86, GIC on ARM) routes
   them to the CPU.

2. **Software interrupts / traps**: The running program calls `INT n` (x86) or `SVC`
   (ARM) — this is the mechanism for syscalls.

3. **Exceptions / faults**: The CPU detects an error (division by zero, page fault,
   invalid instruction) and raises an exception.

For each of these interrupt types the OS registers an **ISR (Interrupt Service Routine)**
in the **IDT (Interrupt Descriptor Table)** on x86 or the **Exception Vector Table** on ARM.

### Interrupt handlers in AIML

```yaml
# Interrupt handler: special function type without normal calling convention
# The CPU automatically saves part of the register state on the stack.
# The handler MUST NOT return normally — it must use `iret` (x86) / `eret` (ARM).

- def:
    name: timer_isr
    attr: [interrupt, naked]    # naked = no prologue/epilogue from compiler
    return: void
    body:
      # Save registers manually (compiler prologue disabled due to naked)
      - cpu.push_all             # pushad / push-all general purpose regs
      # Handler logic
      - call: [timer_tick]
      # Send EOI (End Of Interrupt) to APIC — without this, no further IRQs arrive
      - call: [hw.mmio.write32, 0xFEE000B0, 0]   # APIC EOI Register
      # Restore registers and return with IRET
      - cpu.pop_all
      - cpu.iret

# Register handler in the IDT (x86)
# IDT entry = gate descriptor: handler address + segment selector + flags
- call: [intr.set_handler, 32, timer_isr]   # IRQ0 = interrupt 32 (after PIC remapping)
- call: [intr.set_handler, 14, page_fault_handler]   # Exception 14 = Page Fault
- call: [intr.enable]                        # STI — enable interrupts
```

**Interrupt API:**
```yaml
intr.set_handler(vec: u8, fn: func)      # Register ISR for vector vec
intr.enable()                            # CLI/STI equivalent: enable interrupts
intr.disable()                           # Disable interrupts (critical section)
intr.is_enabled() -> bool
intr.load_idt(base: u64, limit: u16)     # LIDT — load IDT register (x86)
intr.load_gic(base: u64)                 # Set ARM GIC base address
cpu.iret()                               # IRET / ERET — Return from Interrupt
```

---

## Physical Memory Management

### What is physical vs. virtual memory?

Modern CPUs have an **MMU (Memory Management Unit)** that translates virtual addresses
(the addresses a program "sees") to physical addresses (actual RAM slots). This
translation is performed via **page tables** — lookup tables that the OS creates and
that the MMU consults automatically.

When the kernel starts, the MMU is usually still disabled or configured with only a
minimal identity mapping (virtual address = physical address). The OS then must:
1. Inventory the physical RAM (the bootloader provides a memory map)
2. Build a **physical frame allocator** (manages 4KB pages)
3. Create page tables for the kernel + userspace
4. Enable the MMU (write to `CR3` on x86, `TTBR0/1` on ARM)

### Physical Frame Allocator

```yaml
# mem.phys — allocate/free physical pages (no VMM, no mapping)
# A "page" / "frame" = 4096-byte unit of physical RAM

# Initialization: bootloader passes memory map
- def:
    name: init_pmm
    args: [[mmap_base, u64], [mmap_len, u64]]
    body:
      - call: [mem.phys.init, mmap_base, mmap_len]
      # mem.phys.init parses the memory map and marks free frames in a bitmap

# Allocate a physical page (4KB)
- decl: [frame, u64]
- set: [frame, { call: [mem.phys.alloc] }]     # returns physical address or 0
- if:
    condition: { binary: [frame, ==, 0] }
    then:
      - call: [panic, "OOM: no physical frames"]

# Free a page
- call: [mem.phys.free, frame]

# Allocate N contiguous pages (for DMA buffers, page tables)
- decl: [dma_buf, u64]
- set: [dma_buf, { call: [mem.phys.alloc_contiguous, 16] }]  # 16 × 4KB = 64KB
```

### Page Tables & Virtual Memory

```yaml
# mem.virt — page table management (VMM)
# Maps virtual addresses to physical frames with specific permissions

# Create a new address space (e.g. for a new process)
- decl: [pml4, u64]                             # x86_64: Page Map Level 4 (Root)
- set: [pml4, { call: [mem.virt.create_space] }]

# Map a virtual page to a physical frame
- call: [mem.virt.map, pml4, 0xFFFF_8000_0000_0000, frame, {read: true, write: true, exec: false, user: false}]

# Identity map (virt == phys) for early boot phase
- call: [mem.virt.identity_map, pml4, 0x0, 0x0010_0000, {read: true, write: true}]  # first 1MB

# Load address space (writes to CR3 / TTBR0)
- call: [mem.virt.activate, pml4]

# TLB flush (after page table changes)
- call: [mem.virt.flush_tlb, 0xFFFF_8000_0000_0000]   # single page
- call: [mem.virt.flush_tlb_all]                        # full TLB invalidate (slow!)
```

---

## Linker Control

### What does a linker script do?

Normally the linker decides where code and data reside in the address space. For a
kernel the developer *must* control this: the kernel must be loaded at a specific
address, certain sections (`.text`, `.data`, `.bss`, `.rodata`) must reside at known
addresses, and the stack must be initialized in the right place.

AIML allows specifying the linker script directly in the AIML build configuration:

```yaml
# pyproject.toml equivalent for an AIML kernel project:
# kernel.aiml.toml
[build]
target = "freestanding"
arch = "x86_64"
format = "elf"
entry = "kernel_main"

[linker]
script = "kernel.ld"           # path to the linker script
base_addr = 0xFFFF800000100000 # kernel virtual base (higher half)
physical_offset = 0x100000     # kernel loaded physically at 1MB
```

```
/* kernel.ld — Linker Script (GNU ld / lld) */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

SECTIONS {
    . = 0xFFFF800000100000;     /* Higher Half Kernel */

    .text : AT(0x100000) {      /* AT() = physical load address */
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Stack: 16KB, 16-byte aligned */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 0x4000;
        __stack_top = .;
    }
}
```

```yaml
# In AIML code: linker symbols accessible as extern declarations
- extern: [__bss_start, u64]    # symbol from linker script
- extern: [__bss_end,   u64]
- extern: [__stack_top, u64]

# Zero the BSS section (bootloader often does not do this)
- def:
    name: clear_bss
    body:
      - decl: [p, u64]
      - set: [p, __bss_start]
      - while:
          condition: { binary: [p, <, __bss_end] }
          body:
            - call: [hw.mmio.write8, p, 0]
            - set: [p, { binary: [p, +, 1] }]
```

---

## Boot Process

### BIOS Boot (Legacy, x86)

```
BIOS
  └─ POST (Power-On Self Test)
  └─ Search for bootable device
  └─ Load first 512-byte sector (MBR) to 0x7C00
  └─ Jump to 0x7C00

Bootloader (Stage 1 — 512 bytes)
  └─ Load Stage 2 (larger bootloader)
  └─ Switch from Real Mode (16-bit) to Protected Mode (32-bit)
  └─ Load kernel ELF into RAM
  └─ Switch to Long Mode (64-bit, for x86_64)
  └─ Jump to kernel_main
```

### UEFI Boot (Modern)

```
UEFI Firmware
  └─ Initializes hardware
  └─ Searches EFI System Partition (FAT32)
  └─ Loads \EFI\BOOT\BOOTX64.EFI
  └─ UEFI bootloader runs as PE executable

UEFI Bootloader
  └─ Uses UEFI Boot Services (GetMemoryMap, AllocatePages, LoadImage)
  └─ Calls ExitBootServices()   ← no UEFI available after this point
  └─ Jump to kernel_main
```

AIML supports both via the `boot` namespace:

```yaml
# Multiboot2 (BIOS/GRUB) — information GRUB passes to the kernel
- def:
    name: kernel_main
    args: [[magic, u32], [mbi_addr, u64]]   # Multiboot2 calling convention
    body:
      - if:
          condition: { binary: [magic, !=, 0x36D76289] }
          then:
            - call: [panic, "Not booted via Multiboot2"]

      # Parse memory map from Multiboot2 info structure
      - decl: [mmap, u64]
      - set: [mmap, { call: [boot.multiboot2.get_mmap, mbi_addr] }]
      - call: [init_pmm, mmap, { call: [boot.multiboot2.get_mmap_len, mbi_addr] }]

      # Continue with kernel initialization
      - call: [kernel_init]

# UEFI entry point
- def:
    name: efi_main
    args: [[image_handle, u64], [system_table, u64]]
    attr: [uefi_entry]          # AIML sets the correct MS ABI calling convention
    return: u64                 # EFI_STATUS
    body:
      - call: [boot.uefi.init, system_table]
      - decl: [mmap, u64]
      - set: [mmap, { call: [boot.uefi.get_memory_map] }]
      - call: [boot.uefi.exit_boot_services, image_handle]
      - call: [init_pmm, mmap, { call: [boot.uefi.get_mmap_len] }]
      - call: [kernel_init]
      - return: 0
```

---

## CPU Control & Architecture-Specific Operations

### MSR (Model-Specific Registers, x86)

MSRs are special registers of the x86 CPU, accessible with the `RDMSR`/`WRMSR`
instructions. They control features such as the SYSCALL mechanism (`LSTAR` MSR = address
of the syscall handler function), performance counters, and CPU features like the
`NX bit` or `FSGSBASE`.

```yaml
# cpu.rdmsr / cpu.wrmsr — x86 MSR access
- decl: [efer, u64]
- set: [efer, { call: [cpu.rdmsr, 0xC0000080] }]   # IA32_EFER
- set: [efer, { binary: [efer, |, 0x100] }]         # Set SCE bit (Syscall Enable)
- call: [cpu.wrmsr, 0xC0000080, efer]

# Set up SYSCALL: LSTAR = address of syscall_entry handler
- call: [cpu.wrmsr, 0xC0000082, syscall_entry]       # LSTAR MSR

# CPUID — query CPU features
- decl: [features, CpuFeatures]
- set: [features, { call: [cpu.cpuid, 1] }]
- decl: [has_sse4, bool]
- set: [has_sse4, { call: [features.has, "sse4.1"] }]
```

### System Registers (ARM AArch64)

```yaml
# cpu.mrs / cpu.msr — ARM system register access
- decl: [sctlr, u64]
- set: [sctlr, { call: [cpu.mrs, "SCTLR_EL1"] }]   # System Control Register
- set: [sctlr, { binary: [sctlr, |, 1] }]           # Bit 0 = MMU Enable
- call: [cpu.msr, "SCTLR_EL1", sctlr]               # Enable MMU
- cpu.isb                                             # Instruction Synchronization Barrier

# Switch exception level (EL2 → EL1 for Hypervisor → Kernel)
- call: [cpu.drop_to_el1, kernel_el1_entry]
```

### Spinlocks & Atomics

For multicore kernels atomic operations are required. Without them a second CPU core can
enter a critical section simultaneously while the first is still working.

```yaml
# atomic namespace — atomic Read-Modify-Write operations
# Compiles to LOCK-prefix instructions (x86) or LDXR/STXR (ARM)

- decl: [lock, i32]
- set: [lock, 0]

# Spinlock acquire (busy-wait until lock == 0, then set to 1)
- def:
    name: spin_lock
    args: [[l, ptr<i32>]]
    body:
      - while:
          condition: { const: true }
          body:
            - decl: [expected, i32]
            - set: [expected, 0]
            - decl: [got, bool]
            - set: [got, { call: [atomic.compare_exchange, l, expected, 1] }]
            - if:
                condition: got
                then:
                  - return: void
            - cpu.pause   # PAUSE (x86) / YIELD (ARM) — reduces power in spin loop

# atomic API
atomic.load(ptr: ptr<T>) -> T                         # Atomic load
atomic.store(ptr: ptr<T>, val: T)                     # Atomic store
atomic.add(ptr: ptr<T>, val: T) -> T                  # Fetch-and-add
atomic.compare_exchange(ptr, expected, desired) -> bool
atomic.fence(order: string)                           # "acquire" | "release" | "seq_cst"
```

---

## Kernel Components in AIML

### Scheduler

A scheduler decides which thread may run next on which CPU core. The simplest scheduler
is **round-robin**: all threads get a fixed time slice (e.g. 10ms) in turn, then execution
switches.

```yaml
- class:
    name: TaskContext
    members:
      - [id,       u64]
      - [rsp,      u64]       # Saved stack pointer
      - [cr3,      u64]       # Address space (page table root)
      - [state,    u8]        # 0=ready, 1=running, 2=blocked, 3=zombie
      - [priority, i32]

- def:
    name: scheduler_tick      # Called by the timer ISR (every 10ms)
    body:
      - decl: [current, ptr<TaskContext>]
      - set: [current, { call: [scheduler.current_task] }]

      # Saving the current context happens in the ISR (naked handler)
      # Here: only select the next task and switch

      - decl: [next, ptr<TaskContext>]
      - set: [next, { call: [scheduler.next_task] }]    # Round-robin queue

      - if:
          condition: { binary: [next, ==, current] }
          then:
            - return: void          # No switch needed

      # Context switch: save stack pointer, load new one, switch address space
      - call: [cpu.save_context, current]
      - call: [cpu.restore_context, next]
      - call: [mem.virt.activate, next.cr3]
```

### Driver Framework

```yaml
# Drivers register themselves with a device ID
# The kernel matches drivers against detected hardware (e.g. PCI scan)

- class:
    name: Driver
    members:
      - [name,       string]
      - [vendor_id,  u16]     # PCI Vendor ID
      - [device_id,  u16]     # PCI Device ID
      - [probe,      func]    # Called when device is found
      - [remove,     func]    # Called when device is removed

- def:
    name: register_nvme_driver
    body:
      - decl: [drv, Driver]
      - set: [drv.name,      "aiml-nvme"]
      - set: [drv.vendor_id, 0x144D]      # Samsung NVMe
      - set: [drv.device_id, 0xA808]
      - set: [drv.probe,     nvme_probe]
      - set: [drv.remove,    nvme_remove]
      - call: [kernel.drivers.register, drv]

- def:
    name: nvme_probe
    args: [[pci_dev, ptr<PciDevice>]]
    body:
      # Read NVMe BAR0 (Base Address Register = MMIO base of the controller)
      - decl: [bar0, u64]
      - set: [bar0, { call: [pci.read_bar, pci_dev, 0] }]
      # Initialize NVMe controller via MMIO
      - call: [nvme_init_ctrl, bar0]
```

---

## Complete Example: Minimal x86_64 Kernel

```yaml
# boot.aiml — Minimal AIML kernel (x86_64, Multiboot2)
# Outputs "Hello from AIML kernel!" via serial COM1 interface

# ─── VGA Text-Mode Output (simplest display without a driver) ──────────────
# VGA text buffer: 0xB8000, 80×25 characters, 2 bytes each: [char][attrib]

- def:
    name: vga_putc
    args: [[c, u8], [col, u8], [row, u8]]
    body:
      - decl: [offset, u64]
      - set: [offset, { binary: [{ binary: [row, *, 80] }, +, col] }]
      - set: [offset, { binary: [offset, *, 2] }]
      - decl: [addr, u64]
      - set: [addr, { binary: [0xB8000, +, offset] }]
      - call: [hw.mmio.write8, addr,       c]       # ASCII char
      - call: [hw.mmio.write8, { binary: [addr, +, 1] }, 0x0F] # white on black

- def:
    name: vga_print
    args: [[msg, string], [row, u8]]
    body:
      - decl: [i, u8]
      - set: [i, 0]
      - while:
          condition: { binary: [{ call: [msg.char_at, i] }, !=, 0] }
          body:
            - call: [vga_putc, { call: [msg.char_at, i] }, i, row]
            - set: [i, { binary: [i, +, 1] }]

# ─── Serial Output (COM1) ──────────────────────────────────────────────────
- def:
    name: serial_init
    body:
      - call: [hw.port.out8, 0x3F9, 0x00]   # Interrupt disable
      - call: [hw.port.out8, 0x3FB, 0x80]   # DLAB = 1 (baud rate divisor)
      - call: [hw.port.out8, 0x3F8, 0x03]   # Baud rate: 38400 (divisor = 3)
      - call: [hw.port.out8, 0x3F9, 0x00]
      - call: [hw.port.out8, 0x3FB, 0x03]   # 8N1: 8 bits, no parity, 1 stop
      - call: [hw.port.out8, 0x3FC, 0x00]   # No flow control

- def:
    name: serial_putc
    args: [[c, u8]]
    body:
      # Wait until transmitter ready (bit 5 in LSR set)
      - while:
          condition: { binary: [{ binary: [{ call: [hw.port.in8, 0x3FD] }, &, 0x20] }, ==, 0] }
          body: []
      - call: [hw.port.out8, 0x3F8, c]

# ─── Kernel Entry Point ───────────────────────────────────────────────────
- def:
    name: kernel_main
    args: [[magic, u32], [mbi, u64]]
    body:
      # Zero BSS (contains uninitialized global variables)
      - call: [clear_bss]

      # Initialize serial interface
      - call: [serial_init]

      # Output text
      - decl: [msg, string]
      - set: [msg, "Hello from AIML kernel!"]
      - call: [vga_print, msg, 0]

      # And via serial interface
      - decl: [i, u8]
      - set: [i, 0]
      - while:
          condition: { binary: [{ call: [msg.char_at, i] }, !=, 0] }
          body:
            - call: [serial_putc, { call: [msg.char_at, i] }]
            - set: [i, { binary: [i, +, 1] }]

      # Halt loop (CPU waits for interrupt — saves power when idle)
      - while:
          condition: { const: true }
          body:
            - cpu.hlt
```

---

## Supported Architectures

| Architecture   | Status      | Boot methods               | Notes                             |
|----------------|-------------|----------------------------|------------------------------------|
| x86_64         | Full        | BIOS/MBR, Multiboot2, UEFI | Port I/O, APIC, MSR, SSE/AVX      |
| AArch64 (ARM)  | Full        | UEFI, U-Boot, Devicetree   | GIC, MMIO-only, EL0–EL3           |
| RISC-V (64)    | Full        | OpenSBI, U-Boot            | PLIC, CLINT, MMIO-only            |
| i686 (32-bit)  | Basic       | BIOS/MBR, Multiboot        | Protected Mode, no Long Mode      |
| ARM (32-bit)   | Basic       | U-Boot                     | Cortex-M / Cortex-A               |
| WebAssembly    | (no OS)     | Browser / WASI             | No MMIO, no interrupt access      |

---

## Embedded & Microcontroller

AIML can also compile for **microcontrollers** (Cortex-M, AVR, ESP32) without an MMU.
In this mode virtual memory is not available:

```yaml
- target: freestanding
  arch: arm
  cpu: cortex-m4          # Specific CPU variant for better optimization
  fpu: fpv4-sp-d16        # Floating-point unit of the Cortex-M4
  format: raw             # Flat binary (no ELF header)
  no_mmu: true            # No VMM, no mem.virt namespace

# Cortex-M: Reset_Handler = entry point (not main)
- def:
    name: Reset_Handler
    attr: [interrupt, naked, section(".isr_vector")]
    return: void
    body:
      - call: [init_peripherals]
      - call: [main_loop]

# Vector table (Cortex-M specific — first 4 bytes = initial stack pointer)
- data:
    name: vector_table
    attr: [section(".isr_vector"), align(4)]
    values:
      - __stack_top          # Initial SP
      - Reset_Handler        # Reset (entry point)
      - NMI_Handler          # Non-Maskable Interrupt
      - HardFault_Handler    # Unrecoverable Fault
```

---

## Vision: AIML from Kernel to Userspace

The long-term goal is a complete vertical stack in AIML:

```
┌─────────────────────────────────────────────────┐
│           User Applications (hosted)            │ ← Normal AIML programs
│           Web Framework, DB, AI Runtime         │
├─────────────────────────────────────────────────┤
│           AIML Standard Library                 │ ← io, net, sys, db, ...
├─────────────────────────────────────────────────┤
│           Syscall Interface                     │ ← AIML Kernel API
├─────────────────────────────────────────────────┤
│           AIML Microkernel                      │ ← freestanding target
│   Scheduler │ VMM │ IPC │ VFS │ Drivers         │
├─────────────────────────────────────────────────┤
│           Hardware Abstraction Layer            │ ← hw.mmio, hw.port, cpu.*
├─────────────────────────────────────────────────┤
│           x86_64 │ AArch64 │ RISC-V            │
└─────────────────────────────────────────────────┘
```

**Hot-reload in the kernel**: Since drivers can be bundled as `Code` objects, the
`sys.bundle`/`sys.exec` mechanism enables **live driver updates** without a reboot —
analogous to Linux's `modprobe`, but with type safety and version guards.

**AionDB in the kernel**: The `db` namespace can be used in kernel mode as persistent
state storage for kernel subsystems — e.g. for an audit log, device registry, or process
table with query DSL access.

**Containers on bare-metal**: With the `container` namespace (chapter 40) applications
can be partitioned directly on hardware without a host OS — similar to unikernels (OSv,
MirageOS), but with a single, unified language from the transistor level to the web app.
