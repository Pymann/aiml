# OS Support & Bare-Metal Compilation

## Was bedeutet "ein Betriebssystem in AIML schreiben"?

Ein Betriebssystem (OS) ist kein normales Programm. Ein normales Programm läuft *über* einem
Betriebssystem: Es hat Systemaufrufe (syscalls), eine C-Standardbibliothek (libc), einen Heap-
Allocator, einen Stack-Setup-Code (`crt0`) — all das bekommt es geschenkt. Ein Betriebssystem
hingegen muss als erstes Programm nach dem Boot-Loader laufen, ohne jegliche Unterstützung
darunter. Es gibt keine libc. Es gibt kein `printf`. Es gibt keinen Heap. Es gibt keine
Interrupts außer denen, die du selbst einrichtest.

AIML kann in diesen **Freestanding-Modus** wechseln: Die Laufzeitbibliothek (`libaimlrt`) wird
durch eine minimale Bare-Metal-Runtime ersetzt, LLVM erzeugt bare-metal Maschinencode für die
Zielarchitektur, und der Linker bekommt ein benutzerdefiniertes Linker-Script. Damit lässt sich
alles von einem einfachen Bootloader bis zu einem vollen Mikrokern schreiben.

---

## Compilation Targets

AIML kennt zwei grundlegend verschiedene Execution-Targets:

### Hosted Target (Standard)
```yaml
# Standard: Läuft als Prozess unter einem OS (Linux/macOS/Windows)
- target: hosted          # default; libc + libaimlrt verfügbar
```

### Freestanding Target (Bare-Metal)
```yaml
# Bare-Metal: Kein OS darunter
- target: freestanding
  arch: x86_64             # oder: aarch64, riscv64, arm, i686
  format: elf              # oder: pe (UEFI), raw (flat binary)
  entry: kernel_main       # Einstiegspunkt; kein libc _start
```

Im Freestanding-Modus:
- Kein `import io` → kein printf, kein Filesystem
- Kein `import net` → kein Socket
- Kein GC / kein Heap-Allocator (du bringst deinen eigenen mit)
- Kein Thread-Scheduling (du bringst deines mit)
- Verfügbar: `hw` (Hardware-Namespace), `mem.phys`, `cpu`, `intr`

---

## Hardware-Zugriff

### Was ist MMIO?

**Memory-Mapped I/O (MMIO)** ist der Mechanismus, über den ein Prozessor mit
Hardware-Peripherie kommuniziert. Die Hardware (z.B. ein UART-Chip, ein GPIO-Controller,
ein PCIe-Gerät) *mappt* seine Register in den physischen Adressraum. Das bedeutet: Wenn
der Prozessor an Adresse `0xFFFF_C000` schreibt, landen die Bits nicht im RAM, sondern
direkt in einem Register des Peripheriegeräts. Umgekehrt liest ein Read von dieser Adresse
den aktuellen Zustand des Geräts.

Auf x86 gibt es zusätzlich **Port I/O**: ein separater Adressraum (16 Bit, 65536 Ports),
erreichbar mit den speziellen CPU-Instruktionen `IN` und `OUT`. Auf ARM und RISC-V ist
alles MMIO (kein separater Port-Adressraum).

```yaml
# hw.mmio — Memory-Mapped Register Read/Write
# hw.port — x86 Port I/O (IN/OUT Instruktionen)

# Beispiel: UART0 auf einem ARM-Board (z.B. QEMU virt, Adresse 0x09000000)
- decl: [uart_base, u64]
- set: [uart_base, 0x09000000]

# Schreibe ein Byte ('H') in das UART Data Register
- call: [hw.mmio.write8, uart_base, 0x48]       # 'H' = 0x48

# Lese den Status-Register (Offset +5 = Line Status Register)
- decl: [lsr, u8]
- set: [lsr, { call: [hw.mmio.read8, uart_base, 5] }]

# x86: Schreibe in COM1 (Port 0x3F8)
- call: [hw.port.out8, 0x3F8, 0x48]             # 'H'
- decl: [status, u8]
- set: [status, { call: [hw.port.in8, 0x3FD] }] # Line Status Register
```

**MMIO-API:**
```yaml
hw.mmio.read8(addr: u64) -> u8          # Lese 1 Byte von physischer Adresse
hw.mmio.read16(addr: u64) -> u16        # Lese 2 Bytes (LE)
hw.mmio.read32(addr: u64) -> u32        # Lese 4 Bytes (LE)
hw.mmio.read64(addr: u64) -> u64        # Lese 8 Bytes (LE)
hw.mmio.write8(addr: u64, val: u8)
hw.mmio.write16(addr: u64, val: u16)
hw.mmio.write32(addr: u64, val: u32)
hw.mmio.write64(addr: u64, val: u64)
hw.mmio.fence()                         # Memory Barrier (DMB auf ARM, MFENCE auf x86)
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

### Was sind Interrupts?

Der Prozessor führt normalerweise Instruktionen der Reihe nach aus. Ein **Interrupt** ist
ein Signal an die CPU: "Halt kurz an, behandle dieses Ereignis, dann mache weiter." Es gibt
drei Arten:

1. **Hardware-Interrupts**: Externe Geräte signalisieren (Timer-Tick, Tastatur-Eingabe,
   Netzwerk-Paket angekommen). Der Interrupt-Controller (APIC auf x86, GIC auf ARM) leitet
   sie an die CPU weiter.

2. **Software-Interrupts / Traps**: Das laufende Programm ruft `INT n` (x86) oder `SVC`
   (ARM) auf — das ist der Mechanismus für Syscalls.

3. **Exceptions / Faults**: Die CPU erkennt einen Fehler (Division durch Null, Page Fault,
   ungültige Instruktion) und löst eine Exception aus.

Für jeden dieser Interrupt-Typen registriert das OS eine **ISR (Interrupt Service Routine)**
in der **IDT (Interrupt Descriptor Table)** auf x86 oder **Exception Vector Table** auf ARM.

### Interrupt-Handler in AIML

```yaml
# Interrupt Handler: spezieller Funktionstyp ohne normale Calling Convention
# Die CPU sichert automatisch einen Teil des Register-Zustands auf dem Stack.
# Der Handler darf NICHT normal returnen — er muss `iret` (x86) / `eret` (ARM) nutzen.

- def:
    name: timer_isr
    attr: [interrupt, naked]    # naked = kein Prolog/Epilog vom Compiler
    return: void
    body:
      # Sichere Register manuell (Compiler-Prolog deaktiviert wegen naked)
      - cpu.push_all             # pushad / push-all general purpose regs
      # Handler-Logik
      - call: [timer_tick]
      # Sende EOI (End Of Interrupt) an APIC — ohne das, kommen keine weiteren IRQs
      - call: [hw.mmio.write32, 0xFEE000B0, 0]   # APIC EOI Register
      # Stelle Register wieder her und kehre mit IRET zurück
      - cpu.pop_all
      - cpu.iret

# Registriere Handler in der IDT (x86)
# IDT-Eintrag = Gate-Descriptor: Handler-Adresse + Segment-Selector + Flags
- call: [intr.set_handler, 32, timer_isr]   # IRQ0 = Interrupt 32 (nach PIC-Remapping)
- call: [intr.set_handler, 14, page_fault_handler]   # Exception 14 = Page Fault
- call: [intr.enable]                        # STI — Interrupts aktivieren
```

**Interrupt-API:**
```yaml
intr.set_handler(vec: u8, fn: func)      # Registriere ISR für Vektor vec
intr.enable()                            # CLI/STI äquivalent: Interrupts an
intr.disable()                           # Interrupts aus (kritischer Abschnitt)
intr.is_enabled() -> bool
intr.load_idt(base: u64, limit: u16)     # LIDT — lade IDT-Register (x86)
intr.load_gic(base: u64)                 # ARM GIC Basis-Adresse setzen
cpu.iret()                               # IRET / ERET — Return from Interrupt
```

---

## Physisches Speicher-Management

### Was ist physischer vs. virtueller Speicher?

Moderne CPUs haben eine **MMU (Memory Management Unit)**, die virtuelle Adressen (die
Adressen, die ein Programm "sieht") auf physische Adressen (echte RAM-Slots) übersetzt.
Diese Übersetzung erfolgt anhand von **Page Tables** — Lookup-Tabellen, die das OS anlegt
und die die MMU automatisch konsultiert.

Wenn der Kernel startet, ist die MMU meist noch ausgeschaltet oder nur mit einem minimalen
Identity-Mapping (virtuelle Adresse = physische Adresse) konfiguriert. Das OS muss dann:
1. Den physischen RAM inventarisieren (Bootloader gibt eine Memory Map)
2. Einen **Physical Frame Allocator** bauen (verwaltet 4KB-Seiten)
3. Page Tables für den Kernel + Userspace anlegen
4. Die MMU einschalten (`CR3` auf x86, `TTBR0/1` auf ARM schreiben)

### Physical Frame Allocator

```yaml
# mem.phys — Physische Seiten allocieren/freigeben (kein VMM, kein Mapping)
# Eine "Page" / "Frame" = 4096 Byte Einheit des physischen RAMs

# Initialisierung: Bootloader übergibt Memory Map
- def:
    name: init_pmm
    args: [[mmap_base, u64], [mmap_len, u64]]
    body:
      - call: [mem.phys.init, mmap_base, mmap_len]
      # mem.phys.init parst die Memory Map und markiert freie Frames in einer Bitmap

# Allokiere eine physische Seite (4KB)
- decl: [frame, u64]
- set: [frame, { call: [mem.phys.alloc] }]     # returns physische Adresse oder 0
- if:
    condition: { binary: [frame, ==, 0] }
    then:
      - call: [panic, "OOM: no physical frames"]

# Gib Seite zurück
- call: [mem.phys.free, frame]

# Allokiere N zusammenhängende Seiten (für DMA-Buffer, Page Tables)
- decl: [dma_buf, u64]
- set: [dma_buf, { call: [mem.phys.alloc_contiguous, 16] }]  # 16 × 4KB = 64KB
```

### Page Tables & Virtual Memory

```yaml
# mem.virt — Page-Table-Verwaltung (VMM)
# Mappt virtuelle Adressen auf physische Frames mit bestimmten Permissions

# Erstelle neuen Adressraum (z.B. für neuen Prozess)
- decl: [pml4, u64]                             # x86_64: Page Map Level 4 (Root)
- set: [pml4, { call: [mem.virt.create_space] }]

# Mappe virtuelle Seite auf physischen Frame
- call: [mem.virt.map, pml4, 0xFFFF_8000_0000_0000, frame, {read: true, write: true, exec: false, user: false}]

# Identity-Map (virt == phys) für frühe Boot-Phase
- call: [mem.virt.identity_map, pml4, 0x0, 0x0010_0000, {read: true, write: true}]  # erste 1MB

# Lade Adressraum (schreibt in CR3 / TTBR0)
- call: [mem.virt.activate, pml4]

# TLB flush (nach Page-Table-Änderungen)
- call: [mem.virt.flush_tlb, 0xFFFF_8000_0000_0000]   # single page
- call: [mem.virt.flush_tlb_all]                        # full TLB invalidate (langsam!)
```

---

## Linker Control

### Was macht ein Linker-Script?

Normalerweise entscheidet der Linker, wo Code und Daten im Adressraum liegen. Für einen
Kernel *muss* der Entwickler das kontrollieren: Der Kernel muss an einer bestimmten
Adresse geladen werden, bestimmte Sektionen (`.text`, `.data`, `.bss`, `.rodata`) müssen
an bekannten Adressen liegen, und der Stack muss am richtigen Ort initialisiert werden.

AIML erlaubt es, das Linker-Script direkt in der AIML-Build-Konfiguration zu spezifizieren:

```yaml
# pyproject.toml äquivalent für ein AIML-Kernel-Projekt:
# kernel.aiml.toml
[build]
target = "freestanding"
arch = "x86_64"
format = "elf"
entry = "kernel_main"

[linker]
script = "kernel.ld"           # Pfad zum Linker-Script
base_addr = 0xFFFF800000100000 # Kernel Virtual Base (Higher Half)
physical_offset = 0x100000     # Kernel physisch ab 1MB geladen
```

```
/* kernel.ld — Linker-Script (GNU ld / lld) */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kernel_main)

SECTIONS {
    . = 0xFFFF800000100000;     /* Higher Half Kernel */

    .text : AT(0x100000) {      /* AT() = physische Ladeadresse */
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

    /* Stack: 16KB, 16-Byte aligned */
    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_bottom = .;
        . += 0x4000;
        __stack_top = .;
    }
}
```

```yaml
# Im AIML-Code: Linker-Symbole als extern-Deklarationen zugreifbar
- extern: [__bss_start, u64]    # Symbol aus Linker-Script
- extern: [__bss_end,   u64]
- extern: [__stack_top, u64]

# BSS-Sektion nullen (Bootloader macht das oft nicht)
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

## Boot-Prozess

### BIOS-Boot (Legacy, x86)

```
BIOS
  └─ POST (Power-On Self Test)
  └─ Suche bootfähiges Gerät
  └─ Lade ersten 512-Byte-Sektor (MBR) nach 0x7C00
  └─ Springe zu 0x7C00

Bootloader (Stage 1 — 512 Byte)
  └─ Lade Stage 2 (größerer Bootloader)
  └─ Wechsel von Real Mode (16-bit) zu Protected Mode (32-bit)
  └─ Lade Kernel-ELF in RAM
  └─ Wechsel zu Long Mode (64-bit, wenn x86_64)
  └─ Springe zu kernel_main
```

### UEFI-Boot (Modern)

```
UEFI Firmware
  └─ Initialisiert Hardware
  └─ Sucht EFI System Partition (FAT32)
  └─ Lädt \EFI\BOOT\BOOTX64.EFI
  └─ UEFI-Bootloader läuft als PE-Executable

UEFI-Bootloader
  └─ Nutzt UEFI Boot Services (GetMemoryMap, AllocatePages, LoadImage)
  └─ Ruft ExitBootServices()   ← ab hier kein UEFI mehr
  └─ Springe zu kernel_main
```

AIML unterstützt beide über den `boot`-Namespace:

```yaml
# Multiboot2 (BIOS/GRUB) — Information die GRUB an den Kernel übergibt
- def:
    name: kernel_main
    args: [[magic, u32], [mbi_addr, u64]]   # Multiboot2 Calling Convention
    body:
      - if:
          condition: { binary: [magic, !=, 0x36D76289] }
          then:
            - call: [panic, "Not booted via Multiboot2"]

      # Parse Memory Map aus Multiboot2 Info-Struktur
      - decl: [mmap, u64]
      - set: [mmap, { call: [boot.multiboot2.get_mmap, mbi_addr] }]
      - call: [init_pmm, mmap, { call: [boot.multiboot2.get_mmap_len, mbi_addr] }]

      # Weiter mit Kernel-Initialisierung
      - call: [kernel_init]

# UEFI Entry Point
- def:
    name: efi_main
    args: [[image_handle, u64], [system_table, u64]]
    attr: [uefi_entry]          # AIML setzt korrekte MS ABI Calling Convention
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

## CPU-Kontrolle & Architektur-spezifische Operationen

### MSR (Model-Specific Registers, x86)

MSRs sind spezielle Register der x86-CPU, erreichbar mit den `RDMSR`/`WRMSR`-Instruktionen.
Sie steuern Features wie den SYSCALL-Mechanismus (`LSTAR` MSR = Adresse der Syscall-Handler-
Funktion), Performance-Counter, und CPU-Features wie `NX-Bit` oder `FSGSBASE`.

```yaml
# cpu.rdmsr / cpu.wrmsr — x86 MSR Zugriff
- decl: [efer, u64]
- set: [efer, { call: [cpu.rdmsr, 0xC0000080] }]   # IA32_EFER
- set: [efer, { binary: [efer, |, 0x100] }]         # Setze SCE-Bit (Syscall Enable)
- call: [cpu.wrmsr, 0xC0000080, efer]

# SYSCALL einrichten: LSTAR = Adresse des syscall_entry Handlers
- call: [cpu.wrmsr, 0xC0000082, syscall_entry]       # LSTAR MSR

# CPUID — Abfrage CPU-Features
- decl: [features, CpuFeatures]
- set: [features, { call: [cpu.cpuid, 1] }]
- decl: [has_sse4, bool]
- set: [has_sse4, { call: [features.has, "sse4.1"] }]
```

### System-Register (ARM AArch64)

```yaml
# cpu.mrs / cpu.msr — ARM System Register Zugriff
- decl: [sctlr, u64]
- set: [sctlr, { call: [cpu.mrs, "SCTLR_EL1"] }]   # System Control Register
- set: [sctlr, { binary: [sctlr, |, 1] }]           # Bit 0 = MMU Enable
- call: [cpu.msr, "SCTLR_EL1", sctlr]               # MMU einschalten
- cpu.isb                                             # Instruction Synchronization Barrier

# Exception Level wechseln (EL2 → EL1 für Hypervisor → Kernel)
- call: [cpu.drop_to_el1, kernel_el1_entry]
```

### Spinlocks & Atomics

Für Multicore-Kernel braucht man atomare Operationen. Ohne sie kann ein zweiter CPU-Core
gleichzeitig in eine kritische Sektion eintreten, während der erste gerade arbeitet.

```yaml
# atomic namespace — atomare Read-Modify-Write Operationen
# Kompiliert zu LOCK-Prefix-Instruktionen (x86) oder LDXR/STXR (ARM)

- decl: [lock, i32]
- set: [lock, 0]

# Spinlock acquire (busy-wait bis lock == 0, dann auf 1 setzen)
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
            - cpu.pause   # PAUSE (x86) / YIELD (ARM) — reduziert Power im Spin-Loop

# atomic API
atomic.load(ptr: ptr<T>) -> T                         # Atomarer Load
atomic.store(ptr: ptr<T>, val: T)                     # Atomarer Store
atomic.add(ptr: ptr<T>, val: T) -> T                  # Fetch-And-Add
atomic.compare_exchange(ptr, expected, desired) -> bool
atomic.fence(order: string)                           # "acquire" | "release" | "seq_cst"
```

---

## Kernel-Komponenten in AIML

### Scheduler

Ein Scheduler entscheidet, welcher Thread als nächstes auf welchem CPU-Core laufen darf.
Der einfachste Scheduler ist ein **Round-Robin**: Alle Threads kriegen reihum eine feste
Zeitscheibe (z.B. 10ms), dann wird gewechselt.

```yaml
- class:
    name: TaskContext
    members:
      - [id,       u64]
      - [rsp,      u64]       # Gesicherter Stack-Pointer
      - [cr3,      u64]       # Adressraum (Page-Table-Root)
      - [state,    u8]        # 0=ready, 1=running, 2=blocked, 3=zombie
      - [priority, i32]

- def:
    name: scheduler_tick      # Wird vom Timer-ISR aufgerufen (alle 10ms)
    body:
      - decl: [current, ptr<TaskContext>]
      - set: [current, { call: [scheduler.current_task] }]

      # Sicherung des aktuellen Kontexts geschieht in der ISR (naked handler)
      # Hier nur: nächsten Task wählen und switchen

      - decl: [next, ptr<TaskContext>]
      - set: [next, { call: [scheduler.next_task] }]    # Round-Robin Queue

      - if:
          condition: { binary: [next, ==, current] }
          then:
            - return: void          # Kein Wechsel nötig

      # Context Switch: Stack-Pointer sichern, neuen laden, Adressraum wechseln
      - call: [cpu.save_context, current]
      - call: [cpu.restore_context, next]
      - call: [mem.virt.activate, next.cr3]
```

### Treiber-Framework

```yaml
# Treiber registrieren sich mit einer Geräte-ID
# Der Kernel matcht Treiber gegen erkannte Hardware (z.B. PCI-Scan)

- class:
    name: Driver
    members:
      - [name,       string]
      - [vendor_id,  u16]     # PCI Vendor ID
      - [device_id,  u16]     # PCI Device ID
      - [probe,      func]    # Aufgerufen wenn Gerät gefunden
      - [remove,     func]    # Aufgerufen wenn Gerät entfernt

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
      # Lese NVMe BAR0 (Base Address Register = MMIO-Basis des Controllers)
      - decl: [bar0, u64]
      - set: [bar0, { call: [pci.read_bar, pci_dev, 0] }]
      # Initialisiere NVMe Controller über MMIO
      - call: [nvme_init_ctrl, bar0]
```

---

## Vollständiges Beispiel: Minimaler x86_64 Kernel

```yaml
# boot.aiml — Minimaler AIML-Kernel (x86_64, Multiboot2)
# Gibt "Hello from AIML kernel!" über serielle COM1-Schnittstelle aus

# ─── VGA Text-Mode Output (einfachstes Display ohne Treiber) ───────────────
# VGA Text-Buffer: 0xB8000, 80×25 Zeichen, je 2 Bytes: [char][attrib]

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
      - call: [hw.mmio.write8, { binary: [addr, +, 1] }, 0x0F] # weiß auf schwarz

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
      - call: [hw.port.out8, 0x3FB, 0x80]   # DLAB = 1 (Baud rate divisor)
      - call: [hw.port.out8, 0x3F8, 0x03]   # Baud rate: 38400 (divisor = 3)
      - call: [hw.port.out8, 0x3F9, 0x00]
      - call: [hw.port.out8, 0x3FB, 0x03]   # 8N1: 8 Bits, No parity, 1 stop
      - call: [hw.port.out8, 0x3FC, 0x00]   # No flow control

- def:
    name: serial_putc
    args: [[c, u8]]
    body:
      # Warte bis Transmitter bereit (Bit 5 im LSR gesetzt)
      - while:
          condition: { binary: [{ binary: [{ call: [hw.port.in8, 0x3FD] }, &, 0x20] }, ==, 0] }
          body: []
      - call: [hw.port.out8, 0x3F8, c]

# ─── Kernel Entry Point ───────────────────────────────────────────────────
- def:
    name: kernel_main
    args: [[magic, u32], [mbi, u64]]
    body:
      # BSS nullen (enthält uninit. globale Variablen)
      - call: [clear_bss]

      # Serielle Schnittstelle initialisieren
      - call: [serial_init]

      # Text ausgeben
      - decl: [msg, string]
      - set: [msg, "Hello from AIML kernel!"]
      - call: [vga_print, msg, 0]

      # Und über serielle Schnittstelle
      - decl: [i, u8]
      - set: [i, 0]
      - while:
          condition: { binary: [{ call: [msg.char_at, i] }, !=, 0] }
          body:
            - call: [serial_putc, { call: [msg.char_at, i] }]
            - set: [i, { binary: [i, +, 1] }]

      # Halt-Loop (CPU wartet auf Interrupt — im Leerlauf Energie sparen)
      - while:
          condition: { const: true }
          body:
            - cpu.hlt
```

---

## Unterstützte Architekturen

| Architektur    | Status       | Boot-Methoden              | Besonderheiten                    |
|----------------|-------------|----------------------------|------------------------------------|
| x86_64         | Vollständig  | BIOS/MBR, Multiboot2, UEFI | Port I/O, APIC, MSR, SSE/AVX      |
| AArch64 (ARM)  | Vollständig  | UEFI, U-Boot, Devicetree   | GIC, MMIO-only, EL0–EL3           |
| RISC-V (64)    | Vollständig  | OpenSBI, U-Boot            | PLIC, CLINT, MMIO-only            |
| i686 (32-bit)  | Grundlegend  | BIOS/MBR, Multiboot        | Protected Mode, kein Long Mode    |
| ARM (32-bit)   | Grundlegend  | U-Boot                     | Cortex-M / Cortex-A               |
| WebAssembly    | (kein OS)    | Browser / WASI             | Kein MMIO, kein Interrupt-Zugriff |

---

## Embedded & Microcontroller

AIML kann auch auf **Microcontrollern** (Cortex-M, AVR, ESP32) ohne MMU kompilieren.
In diesem Modus ist virtueller Speicher nicht verfügbar:

```yaml
- target: freestanding
  arch: arm
  cpu: cortex-m4          # Spezifische CPU-Variante für bessere Optimierung
  fpu: fpv4-sp-d16        # Floating-Point-Unit des Cortex-M4
  format: raw             # Flat binary (kein ELF-Header)
  no_mmu: true            # Kein VMM, kein mem.virt Namespace

# Cortex-M: Reset_Handler = Einstiegspunkt (nicht main)
- def:
    name: Reset_Handler
    attr: [interrupt, naked, section(".isr_vector")]
    return: void
    body:
      - call: [init_peripherals]
      - call: [main_loop]

# Vector Table (Cortex-M spezifisch — erste 4 Bytes = Initial Stack Pointer)
- data:
    name: vector_table
    attr: [section(".isr_vector"), align(4)]
    values:
      - __stack_top          # Initial SP
      - Reset_Handler        # Reset (Einstiegspunkt)
      - NMI_Handler          # Non-Maskable Interrupt
      - HardFault_Handler    # Unrecoverable Fault
```

---

## Vision: AIML von Kernel bis Userspace

Das langfristige Ziel ist ein vollständiger Vertikaler Stack in AIML:

```
┌─────────────────────────────────────────────────┐
│           User Applications (hosted)            │ ← Normale AIML-Programme
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

**Hot-Reload im Kernel**: Da Treiber als `Code`-Objekte gebündelt werden können, ermöglicht
der `sys.bundle`/`sys.exec`-Mechanismus **Live-Treiber-Updates** ohne Neustart — analog zu
Linux's `modprobe`, aber mit Typ-Sicherheit und Versions-Guards.

**AionDB im Kernel**: Der `db`-Namespace kann im Kernel-Modus als persistenter Zustands-
Speicher für Kernel-Subsysteme verwendet werden — z.B. für ein Audit-Log, Device-Registry,
oder Process-Table mit Query-DSL-Zugriff.

**Container auf Bare-Metal**: Mit dem `container`-Namespace (Kapitel 40) können Anwendungen
direkt auf Hardware partitioniert werden, ohne Host-OS — ähnlich wie Unikernels (OSv, MirageOS),
aber mit einer einzigen, einheitlichen Sprache von der Transistor-Ebene bis zur Web-App.
