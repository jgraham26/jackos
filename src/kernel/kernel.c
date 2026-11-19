#include "kernel.h"
#include <stdint.h>
#include <string.h>
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/syscall.h"
#include "drivers/serial.h"
#include "drivers/terminal.h"
#include "drivers/ata.h"
#include "drivers/ps2_kbd.h"
#include "fs/urandom.h"
#include "mem/pmm.h"
#include "mem/vmm.h"
#include "mem/kheap.h"
#include "boot/bootinfo.h"
#include "boot/elf.h"
#include "cpu/apic.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "fs/bdev.h"
#include "fs/devfs.h"
#include "fs/mbr.h"
#include "fs/ext2.h"
#include <drivers/rtc.h>
#include "mem/kstack.h"
#include "cpu/percpu.h"
#include "fs/proc.h"
#include "fs/fbdev.h"
#include "fs/urandom.h"
#include "fs/kbdev.h"
#include "drivers/klog.h"

void __attribute__((noreturn)) hcf() {
    while (1) {
        __asm__ volatile ("cli; hlt");
    }
}

void __attribute__((noreturn)) idle() {
    while (1) {
        __asm__ volatile ("sti; hlt");
    }
}

vfs_node_t *tarfs_init(uintptr_t archive_start, size_t archive_size);

void __attribute__((noreturn)) kmain(uintptr_t mb2_phys_addr) {
    /* 1. Core Hardware & Memory Subsystems */
    init_serial();
    klog_init();
    init_gdt();
    klog_puts("[KERNEL] Initialized GDT\n");

    init_idt();
    klog_puts("[KERNEL] Initialized IDT\n");

    boot_info_parse(mb2_phys_addr);
    klog_puts("[KERNEL] Parsed boot info\n");

    pmm_init(mb2_phys_addr);
    klog_puts("[KERNEL] Initialized PMM\n");

    vmm_init();
    kstack_init();
    klog_puts("[KERNEL] Initialized VMM\n");

    apic_init();
    klog_puts("[KERNEL] Initialized APIC\n");

    syscall_init();
    percpu_init();
    klog_puts("[KERNEL] Initialized Syscalls\n");

    sched_init();
    klog_puts("[KERNEL] Initialized Scheduler\n");

    apic_timer_init(1000);
    klog_puts("[KERNEL] Initialized APIC timer\n");

    kbd_init();
    klog_puts("[KERNEL] Initialized Keyboard\n");

    init_terminal(&g_boot_info);
    klog_puts("[KERNEL] Initialized Terminal\n");

    /* 2. Initialize VFS Layer & Block Device Subsystem */
    vfs_init();
    bdev_init();
    devfs_init();
    rtc_init();
    klog_puts("[KERNEL] Initialized vfs and rtc");

    /* 3. Probe Storage Hardware & Partition Table */
    if (ata_init() == 0) {
        klog_puts("[ATA] Primary master drive detected\n");
        ata_bdev_register();

        block_device_t *hda = bdev_get("hda");
        if (hda) {
            devfs_register_bdev(hda);
            klog_puts("[DEVFS] Registered /dev/hda\n");

            /* Read MBR: creates child bdevs like hda1 (FAT32 boot) and hda2 (ext2 root) */
            if (mbr_scan_and_register(hda) == 0) {
                klog_puts("[MBR] Partition table scan complete\n");
            } else {
                klog_puts("[MBR] No valid MBR signature found on /dev/hda\n");
            }
        }
    } else {
        klog_puts("[ATA] No primary master drive found\n");
    }

    /* 4. Mount Root Filesystem: Prefer ext2 partition (/dev/hda2), fallback to ramdisk tarfs */
    block_device_t *ext2_part = bdev_get("hda2");

    if (ext2_part) {
        vfs_node_t *ext2_root = ext2_mount(ext2_part);
        if (ext2_root) {
            vfs_mount("/", ext2_root);
            klog_puts("[VFS] Mounted /dev/hda2 as root (ext2)\n");
        } else {
            klog_puts("[KERNEL] Panic: Unable to mount any root filesystem!\n");
            hcf();
        }
    }

    vfs_mkdir("/proc", 0555);
    vfs_node_t* proc_root = procfs_create_root();
    if (proc_root) {
        vfs_mount("/proc", proc_root);
    }

    /* 5. Re-attach devfs to root /dev */
    vfs_mount("/dev", devfs_get_root());
    fbdev_init();
    urandom_init();
    kbddev_init();
    klog_devfs_init();

    /* 6. Verify and Log Root Contents */
    vfs_file_t *root_dir = vfs_open("/bin", O_RDONLY);
    if (root_dir) {
        klog_puts("[VFS] /bin directory contents:\n");
        vfs_dirent_t entry;
        size_t idx = 0;
        while (root_dir->node->ops->readdir(root_dir->node, idx++, &entry) == 0) {
            klog_printf("  -> %s [%s]\n", entry.name, (entry.type == VFS_DIRECTORY) ? "DIR" : "FILE");
        }
        vfs_close(root_dir);
    }

    /* 7. Load and Spawn Initial Userspace Process (/bin/init or /init) */
    size_t init_size = 0;
    void *init_elf_data = vfs_read_entire_file("/bin/init", &init_size);

    if (!init_elf_data) {
        init_elf_data = vfs_read_entire_file("/init", &init_size);
    }

    if (!init_elf_data) {
        klog_printf("[KERNEL] Panic: Could not find /bin/init or /init on root filesystem!\n");
        hcf();
    }

    klog_printf("[KERNEL] Spawning /bin/init (%llu bytes)...\n", (uint64_t)init_size);

    process_t *init_proc = elf_load_process(init_elf_data, init_size);
    if (!init_proc) {
        klog_printf("[KERNEL] Panic: Failed to load /bin/init ELF!\n");
        hcf();
    }

    kfree(init_elf_data);

    klog_printf("[KERNEL] Userspace init started. Entering scheduler idle loop...\n");


    /* 8. Enable Interrupts and Enter Idle Loop */
    sti();
    idle();
}