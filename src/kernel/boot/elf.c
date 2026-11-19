#include "boot/elf.h"
#include "mem/vmm.h"
#include "mem/pmm.h"
#include "mem/kheap.h"
#include "fs/vfs.h"
#include "sched/sched.h"
#include <string.h>
#include <stdio.h>

#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

bool elf_validate(const elf64_ehdr_t *hdr) {
    if (!hdr) return false;
    if (*(uint32_t *)hdr->e_ident != ELF_MAGIC) return false;
    if (hdr->e_ident[4] != 2) return false;
    if (hdr->e_ident[5] != 1) return false;
    if (hdr->e_machine != 0x3E) return false;
    return true;
}

static int elf_load_segments(const void *elf_data, size_t size, vmm_pagemap_t *pagemap,
                             uintptr_t load_bias,
                             uintptr_t *highest_vaddr_out, uintptr_t *phdr_vaddr_out,
                             char *interp_path_out, size_t interp_path_max) {
    const elf64_ehdr_t *hdr = (const elf64_ehdr_t *)elf_data;
    const uint8_t *base = (const uint8_t *)elf_data;
    const elf64_phdr_t *phdrs = (const elf64_phdr_t *)(base + hdr->e_phoff);

    uintptr_t highest_vaddr = 0;
    uintptr_t phdr_vaddr = 0;
    if (interp_path_out && interp_path_max > 0) {
        interp_path_out[0] = '\0';
    }

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t *ph = &phdrs[i];

        if (ph->p_type == PT_INTERP && interp_path_out && interp_path_max > 0) {
            if (ph->p_offset + ph->p_filesz > size || ph->p_filesz == 0 ||
                ph->p_filesz >= interp_path_max) {
                return -1;
            }
            memcpy(interp_path_out, base + ph->p_offset, ph->p_filesz);
            interp_path_out[ph->p_filesz] = '\0';
            continue;
        }

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        if (ph->p_offset + ph->p_filesz > size) {
            return -1;
        }

        uintptr_t seg_vaddr = ph->p_vaddr + load_bias;

        if (hdr->e_phoff >= ph->p_offset && hdr->e_phoff < (ph->p_offset + ph->p_filesz)) {
            phdr_vaddr = seg_vaddr + (hdr->e_phoff - ph->p_offset);
        }

        uint64_t vaddr_start = PAGE_ALIGN_DOWN(seg_vaddr);
        uint64_t vaddr_end   = PAGE_ALIGN_UP(seg_vaddr + ph->p_memsz);
        if (vaddr_end > highest_vaddr) {
            highest_vaddr = vaddr_end;
        }

        uint64_t flags = PTE_PRESENT | PTE_USER;
        if (ph->p_flags & PF_W) {
            flags |= PTE_WRITABLE;
        }

        for (uint64_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += PAGE_SIZE) {
            pt_entry_t *existing_pte = vmm_get_pte(pagemap, vaddr);
            if (!existing_pte || !(*existing_pte & PTE_PRESENT)) {
                uintptr_t phys_frame = pmm_alloc_frame();
                if (!phys_frame) return -1;

                void *frame_kptr = (void *)hhdm_physical_to_virtual(phys_frame);
                memset(frame_kptr, 0, PAGE_SIZE);
                vmm_map_page(pagemap, vaddr, phys_frame, flags);
            } else if (ph->p_flags & PF_W) {
                *existing_pte |= PTE_WRITABLE;
                vmm_invlpg(vaddr);
            }
        }

        uint64_t copied = 0;
        while (copied < ph->p_filesz) {
            uint64_t cur_vaddr = seg_vaddr + copied;
            uint64_t page_base = PAGE_ALIGN_DOWN(cur_vaddr);
            uint64_t page_off  = cur_vaddr - page_base;
            uint64_t chunk     = PAGE_SIZE - page_off;

            if (chunk > (ph->p_filesz - copied)) {
                chunk = ph->p_filesz - copied;
            }

            pt_entry_t *pte = vmm_get_pte(pagemap, page_base);
            uintptr_t phys = *pte & PTE_FRAME_ADDR_MASK;
            uint8_t *kdest = (uint8_t *)hhdm_physical_to_virtual(phys) + page_off;
            const uint8_t *ksrc = base + ph->p_offset + copied;

            memcpy(kdest, ksrc, chunk);
            copied += chunk;
        }
    }

    if (highest_vaddr_out) *highest_vaddr_out = highest_vaddr;
    if (phdr_vaddr_out)    *phdr_vaddr_out = phdr_vaddr;
    return 0;
}

uintptr_t setup_user_stack(vmm_pagemap_t *pagemap,
                           uintptr_t stack_top_virt,
                           const elf64_ehdr_t *ehdr,
                           uintptr_t phdr_vaddr,
                           uintptr_t main_entry,
                           uintptr_t interp_base,
                           int argc,
                           const char *argv[],
                           const char *envp[]) {

    /* Locate the physical backing frame of the top stack page via HHDM */
    pt_entry_t *pte = vmm_get_pte(pagemap, stack_top_virt - PAGE_SIZE);
    if (!pte || !(*pte & PTE_PRESENT)) {
        return 0;
    }

    uintptr_t phys_frame = *pte & PTE_FRAME_ADDR_MASK;
    uint8_t *kpage = (uint8_t *)hhdm_physical_to_virtual(phys_frame);
    uintptr_t upage = stack_top_virt - PAGE_SIZE;

    /* Start at top of the page */
    size_t offset = PAGE_SIZE;

    int envc = 0;
    if (envp) {
        while (envp[envc]) envc++;
    }

    uint64_t u_argv[argc];
    uint64_t u_envp[envc];

    /* 1. Place envp strings at top of page */
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        offset -= len;
        memcpy(kpage + offset, envp[i], len);
        u_envp[i] = upage + offset;
    }

    /* 2. Place argv strings */
    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        offset -= len;
        memcpy(kpage + offset, argv[i], len);
        u_argv[i] = upage + offset;
    }

    /* 3. 16-byte random seed for AT_RANDOM */
    offset -= 16;
    uint64_t *rand_ptr = (uint64_t *)(kpage + offset);
    rand_ptr[0] = 0x517CC1B727220A95ULL;
    rand_ptr[1] = 0x110599A458E7CD14ULL;
    uint64_t u_rand_addr = upage + offset;

    /* Align offset down to 8 bytes */
    offset &= ~7ULL;

    /* Helper macro to push a 64-bit value to the stack page */
    #define PUSH64(val) do { \
        offset -= 8; \
        *(uint64_t *)(kpage + offset) = (uint64_t)(val); \
    } while (0)

    /*
     * We need final RSP to be 16-byte aligned: (offset % 16) == 0.
     * Calculate total 8-byte words to be pushed:
     *   Auxv: 7 pairs = 14 words
     *   envp: (envc + 1) words
     *   argv: (argc + 1) words
     *   argc: 1 word
     */
    size_t total_words = 16 + (envc + 1) + (argc + 1) + 1;
    if (((offset - (total_words * 8)) % 16) != 0) {
        PUSH64(0);
    }

    /* --- Auxiliary Vector --- */
    PUSH64(0); PUSH64(AT_NULL);
    PUSH64(u_rand_addr); PUSH64(AT_RANDOM);
    PUSH64(main_entry); PUSH64(AT_ENTRY);
    PUSH64(interp_base); PUSH64(AT_BASE);
    PUSH64(ehdr->e_phnum); PUSH64(AT_PHNUM);
    PUSH64(ehdr->e_phentsize); PUSH64(AT_PHENT);
    PUSH64(phdr_vaddr); PUSH64(AT_PHDR);
    PUSH64(PAGE_SIZE); PUSH64(AT_PAGESZ);

    /* --- Environment Pointers --- */
    PUSH64(0); /* NULL terminator */
    for (int i = envc - 1; i >= 0; i--) {
        PUSH64(u_envp[i]);
    }

    /* --- Argument Pointers --- */
    PUSH64(0); /* NULL terminator */
    for (int i = argc - 1; i >= 0; i--) {
        PUSH64(u_argv[i]);
    }

    /* --- Argument Count --- */
    PUSH64(argc);

    #undef PUSH64

    /* Return virtual address pointing directly to argc */
    return upage + offset;
}

process_t *elf_load_process(const void *elf_data, size_t size) {
    const elf64_ehdr_t *hdr = (const elf64_ehdr_t *)elf_data;
    if (!elf_validate(hdr)) return NULL;

    process_t *proc = process_create();
    if (!proc) return NULL;

    uintptr_t main_entry = 0;
    uintptr_t heap_start = 0;
    uintptr_t phdr_vaddr = 0;
    elf64_ehdr_t saved_ehdr;
    char interp_path[VFS_MAX_PATH];

    if (elf_load_binary_into_pagemap(elf_data, size, proc->pagemap, 0,
                                     &main_entry, &heap_start, &phdr_vaddr,
                                     &saved_ehdr, interp_path, sizeof(interp_path)) != 0) {
        process_destroy(proc);
        return NULL;
    }

    proc->heap_start = heap_start;
    proc->heap_end   = heap_start;

    uintptr_t start_rip   = main_entry;
    uintptr_t interp_base = 0;

    /* Support dynamically linked /bin/init via PT_INTERP */
    if (interp_path[0] != '\0') {
        size_t interp_size = 0;
        void *interp_buf = vfs_read_entire_file(interp_path, &interp_size);
        if (!interp_buf) {
            process_destroy(proc);
            return NULL;
        }

        uintptr_t interp_entry = 0;
        if (elf_load_binary_into_pagemap(interp_buf, interp_size, proc->pagemap,
                                         INTERP_LOAD_BASE, &interp_entry,
                                         NULL, NULL, NULL, NULL, 0) != 0) {
            kfree(interp_buf);
            process_destroy(proc);
            return NULL;
        }
        kfree(interp_buf);

        interp_base = INTERP_LOAD_BASE;
        start_rip   = interp_entry;
    }

    /* Allocate user stack */
    for (uintptr_t s = USER_STACK_VIRT_TOP - (4 * PAGE_SIZE); s < USER_STACK_VIRT_TOP; s += PAGE_SIZE) {
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) {
            process_destroy(proc);
            return NULL;
        }
        void *kptr = (void *)hhdm_physical_to_virtual(frame);
        memset(kptr, 0, PAGE_SIZE);
        vmm_map_page(proc->pagemap, s, frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }

    const char *default_argv[] = { "init", NULL };
    const char *default_envp[] = { "PATH=/bin", "TERM=xterm", NULL };

    uintptr_t user_rsp = setup_user_stack(
        proc->pagemap,
        USER_STACK_VIRT_TOP,
        &saved_ehdr,
        phdr_vaddr,
        main_entry,
        interp_base,
        1,
        default_argv,
        default_envp
    );

    if (!user_rsp) {
        process_destroy(proc);
        return NULL;
    }

    thread_t *t = thread_create_user((void (*)(void))start_rip, proc->pagemap, proc, user_rsp);
    if (!t) {
        process_destroy(proc);
        return NULL;
    }

    return proc;
}

int elf_load_binary_into_pagemap(const void *elf_data,
                                 size_t size,
                                 vmm_pagemap_t *dst_map,
                                 uintptr_t load_bias,
                                 uintptr_t *entry_out,
                                 uintptr_t *heap_start_out,
                                 uintptr_t *phdr_vaddr_out,
                                 elf64_ehdr_t *saved_ehdr,
                                 char *interp_path_out,
                                 size_t interp_path_max) {
    const elf64_ehdr_t *hdr = (const elf64_ehdr_t *)elf_data;
    if (!elf_validate(hdr)) return -1;

    if (hdr->e_type == ET_DYN && load_bias == 0) {
        load_bias = PIE_LOAD_BASE;
    }

    uintptr_t highest_vaddr = 0;
    uintptr_t phdr_vaddr = 0;

    if (elf_load_segments(elf_data, size, dst_map, load_bias,
                          &highest_vaddr, &phdr_vaddr,
                          interp_path_out, interp_path_max) != 0) {
        return -1;
    }

    if (entry_out)      *entry_out = hdr->e_entry + load_bias;
    if (heap_start_out) *heap_start_out = highest_vaddr;
    if (phdr_vaddr_out) *phdr_vaddr_out = phdr_vaddr;
    if (saved_ehdr)     memcpy(saved_ehdr, hdr, sizeof(elf64_ehdr_t));
    return 0;
}