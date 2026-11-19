#pragma once


#include <stdint.h>
#include "stdbool.h"
#include "sched/process.h"


#define ELF_MAGIC 0x464C457F /* "\x7FELF" */

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

#define ET_EXEC 2
#define ET_DYN  3

#define PIE_LOAD_BASE    0x0000000000400000ULL
#define INTERP_LOAD_BASE 0x00007F0000000000ULL

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

bool elf_validate(const elf64_ehdr_t *hdr);
process_t *elf_load_process(const void *elf_data, size_t size);

#define AT_NULL   0   /* End of vector */
#define AT_IGNORE 1   /* Entry should be ignored */
#define AT_EXECFD 2   /* File descriptor of program */
#define AT_PHDR   3   /* Program headers for program */
#define AT_PHENT  4   /* Size of program header entry */
#define AT_PHNUM  5   /* Number of program headers */
#define AT_PAGESZ 6   /* System page size */
#define AT_BASE   7   /* Base address of interpreter (if dynamic) */
#define AT_FLAGS  8   /* Flags */
#define AT_ENTRY  9   /* Entry point of program */
#define AT_NOTELF 10  /* Program is not ELF */
#define AT_UID    11  /* Real uid */
#define AT_EUID   12  /* Effective uid */
#define AT_GID    13  /* Real gid */
#define AT_EGID   14  /* Effective gid */
#define AT_RANDOM 25  /* Address of 16 random bytes */
#define AT_SECURE 23  /* Boolean, secure mode */

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} elf64_auxv_t;

int elf_load_binary_into_pagemap(const void *elf_data,
                                 size_t size,
                                 vmm_pagemap_t *dst_map,
                                 uintptr_t load_bias,
                                 uintptr_t *entry_out,
                                 uintptr_t *heap_start_out,
                                 uintptr_t *phdr_vaddr_out,
                                 elf64_ehdr_t *saved_ehdr,
                                 char *interp_path_out,
                                 size_t interp_path_max);

uintptr_t setup_user_stack(vmm_pagemap_t *pagemap,
                           uintptr_t stack_top_virt,
                           const elf64_ehdr_t *ehdr,
                           uintptr_t phdr_vaddr,
                           uintptr_t main_entry,
                           uintptr_t interp_base,
                           int argc,
                           const char *argv[],
                           const char *envp[]);