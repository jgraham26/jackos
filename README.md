# JackOS

A Unix-like, 64-bit higher-half operating system kernel (`x86_64`) written from scratch in C and x86-64 assembly, complete with a POSIX-style Virtual File System, `ext2` root filesystem, preemptive multitasking, Ring 3 user space powered by a ported `musl` libc and `BusyBox`, and support for running `DOOM` (`doomgeneric`).

---

## Architecture & Key Features

### Boot & CPU Management (`src/kernel/boot/`, `src/kernel/cpu/`)
* **Multiboot2 Compliant:** Boots via a custom two-stage BIOS bootloader (or GRUB), parsing E820 memory maps, VBE linear framebuffers, and ACPI RSDP tables (`boot_info.c`).
* **Higher-Half Kernel:** Maps the kernel at `0xFFFFFFFF80000000` alongside a Higher-Half Direct Map (HHDM) using 2 MiB huge pages for physical memory access.
* **Interrupts & SMP-Ready Structures:** Configures GDT/TSS (`gdt.c`, `gdt.asm`), IDT (`idt.c`, `idt.asm`), Local APIC / I/O APIC routing (`apic.c`), per-CPU data structures (`percpu.c`), FPU state management (`fpu.h`), and fast `SYSCALL`/`SYSRET` user-kernel transitions (`syscall.c`, `syscall.asm`).
* **ELF64 Loader:** Parses and loads 64-bit user-space ELF executables (`elf.c`).

### Memory Management (`src/kernel/mem/`)
* **Physical Memory Manager (`pmm.c`):** O(1)-amortized 64-bit word-scanning bitmap allocator with contiguous multi-frame allocation support.
* **Virtual Memory Manager (`vmm.c`):** 4-level x86-64 paging (`PML4` → `PDPT` → `PD` → `PT`) supporting 4 KiB and 2 MiB huge pages, per-process address space creation (`vmm_new_pagemap`), deep address-space cloning for `fork()` (`vmm_clone_pagemap`), and safe user-pointer validation (`uaccess.h`).
* **Kernel Heap & Stacks (`kheap.c`, `kstack.c`):** Spinlock-synchronized doubly-linked free-list allocator with block splitting and coalescing, plus dedicated kernel stack management.

### Process Scheduling & Signals (`src/kernel/sched/`)
* **Preemptive Multitasking (`sched.c`, `process.c`, `switch_to.asm`):** Process lifecycle management, context switching, and SMP-safe spinlocks (`spinlock.h`).
* **POSIX Signals (`signal.c`):** Signal delivery and handling across user/kernel boundaries.

### Filesystems & Storage (`src/kernel/fs/`)
* **Virtual File System (`vfs.c`):** Unified mount table and file descriptor abstraction supporting multiple filesystems and special files:
  * **`ext2` (`ext2.c`):** Read/write `ext2` filesystem driver for the root partition.
  * **`tarfs` (`tarfs.c`):** USTAR archive filesystem support.
  * **`devfs` (`devfs.c`):** Device filesystem exposing `/dev/fb0` (`fbdev.c`), `/dev/kbd` (`kbdev.c`), and `/dev/urandom` (`urandom.c`).
  * **`proc` (`proc.c`) & `pipe` (`pipe.c`):** Process status pseudo-filesystem and anonymous IPC pipes.
* **Block Layer (`bdev.c`, `mbr.c`):** MBR partition table parser and block device abstraction.

### Device Drivers (`src/kernel/drivers/`)
* **Storage:** ATA PIO disk driver (`ata.c`) and RAM disk driver (`ramdisk.c`).
* **Timers & Clock:** Programmable Interval Timer (`pit.c`) and CMOS Real-Time Clock (`rtc.c`).
* **Input & Console:** PS/2 keyboard driver (`ps2_kbd.c`), 16550 UART serial console (`serial.c`), and VBE linear framebuffer terminal emulator with bitmap font rendering (`terminal.c`, `font.h`).

---

## Userspace & Ports (`src/prog/`)

JackOS runs Ring 3 user-space binaries compiled with a custom `x86_64-jackos` GCC/Binutils toolchain:
* **`musl` libc (`src/prog/musl/`):** Ported C standard library targeting JackOS's syscall ABI.
* **`BusyBox` (`src/prog/busybox/`):** Ported `BusyBox 1.36.1` providing standard Unix utilities and shell environment.
* **Native Utilities:** Custom `/bin/init` (`src/prog/init/`) alongside standalone user programs (`sh`, `ls`, `cat`, `echo`, `touch`, `clear`, `hello`).
* **DOOM:** Supports running `doomgeneric` rendering directly to the `/dev/fb0` framebuffer.

---

## Repository Structure

```text
.
├── Makefile                  # Top-level build, disk image assembly, QEMU & Bochs targets
├── boot/                     # Custom two-stage Multiboot2 bootloader (Git submodule)
├── include/
│   ├── kernel/               # Kernel headers (boot, cpu, drivers, fs, mem, sched)
│   └── libk/                 # Freestanding kernel C library headers
├── scripts/
│   └── build_disk.sh         # Rootless dual-partition (FAT32 + ext2) disk image builder
├── src/
│   ├── kernel/               # x86-64 kernel source & linker script (linker.ld)
│   ├── libk/                 # Freestanding kernel utility library (libk.a)
│   └── prog/                 # Userspace: musl libc, BusyBox, init, coreutils, user.ld
└── build/                    # Out-of-tree build artifacts & disk.img (generated)