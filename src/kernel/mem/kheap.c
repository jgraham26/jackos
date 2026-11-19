#include "mem/kheap.h"
#include "mem/vmm.h"
#include "mem/pmm.h"
#include "sched/spinlock.h"
#include "drivers/klog.h"
#include "kernel.h"
#include <stdio.h>
#include <string.h>

static spinlock_t s_kheap_lock = SPINLOCK_INIT;

#define HEAP_ALIGNMENT 16
#define ALIGN_UP(val, align)   (((val) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(val, align) ((val) & ~((align) - 1))

typedef struct heap_node {
    size_t size;               /* Usable capacity excluding header */
    int is_free;               /* 1 = free, 0 = allocated */
    struct heap_node *prev;    /* Doubly linked traversal for O(1) coalescing */
    struct heap_node *next;
} heap_node_t;

#define HEADER_SIZE ALIGN_UP(sizeof(heap_node_t), HEAP_ALIGNMENT)
#define MIN_SPLIT_SIZE (HEADER_SIZE + HEAP_ALIGNMENT)

static uintptr_t s_heap_start = 0;
static uintptr_t s_heap_curr_top = 0;
static uintptr_t s_heap_max = 0;
static heap_node_t *s_head = NULL;

static bool kheap_expand(size_t bytes) {
    bytes = ALIGN_UP(bytes, PAGE_SIZE);

    if (s_heap_curr_top + bytes > s_heap_max) {
        return false;
    }


    uintptr_t new_top = s_heap_curr_top + bytes;

    for (uintptr_t virt = s_heap_curr_top; virt < new_top; virt += PAGE_SIZE) {
        uintptr_t frame = pmm_alloc_frame();
        if (!frame) {
            klog_printf("[KHEAP] failed to allocate new physical frame\n");
            pmm_dump();
            return false;
        }
        if (vmm_map_page(&kernel_pagemap, virt, frame, PTE_WRITABLE) != 0) {
            pmm_free_frame(frame);
            return false;
        }
    }

    /* Locate tail node to append or coalesce */
    heap_node_t *tail = s_head;
    while (tail && tail->next) {
        tail = tail->next;
    }

    uintptr_t new_mem = s_heap_curr_top;
    s_heap_curr_top = new_top;

    if (tail && tail->is_free) {
        /* Merge expanded region straight into current free tail */
        tail->size += bytes;
    } else {
        /* Create new free block header at old boundary */
        heap_node_t *expanded_block = (heap_node_t *)new_mem;
        expanded_block->size = bytes - HEADER_SIZE;
        expanded_block->is_free = 1;
        expanded_block->next = NULL;
        expanded_block->prev = tail;

        if (tail) {
            tail->next = expanded_block;
        } else {
            s_head = expanded_block;
        }
    }

    return true;
}

void kheap_init(uintptr_t virt_start, size_t initial_bytes, size_t max_bytes) {
    s_heap_start = ALIGN_UP(virt_start, PAGE_SIZE);
    s_heap_curr_top = s_heap_start;
    s_heap_max = s_heap_start + max_bytes;
    s_head = NULL;

    if (!kheap_expand(initial_bytes)) {
        serial_puts("[KHEAP] Failed to initialize requested initial heap size.\n");
        hcf();
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    uint64_t flags = spinlock_irqsave(&s_kheap_lock);

    size = ALIGN_UP(size, HEAP_ALIGNMENT);

    for (heap_node_t *curr = s_head; curr != NULL; curr = curr->next) {
        if (!curr->is_free || curr->size < size) {
            continue;
        }

        /* Check if node can be split */
        if (curr->size >= size + MIN_SPLIT_SIZE) {
            heap_node_t *split = (heap_node_t *)((uintptr_t)curr + HEADER_SIZE + size);
            split->size = curr->size - size - HEADER_SIZE;
            split->is_free = 1;
            split->prev = curr;
            split->next = curr->next;

            if (curr->next) {
                curr->next->prev = split;
            }

            curr->next = split;
            curr->size = size;
        }

        curr->is_free = 0;
        spinlock_irqrestore(&s_kheap_lock, flags);
        return (void *)((uintptr_t)curr + HEADER_SIZE);
    }

    /* Heap exhausted: expand memory window */
    size_t expand_needed = size + HEADER_SIZE;
    if (expand_needed < PAGE_SIZE * 4) {
        expand_needed = PAGE_SIZE * 4;
    }

    if (kheap_expand(expand_needed)) {
        spinlock_irqrestore(&s_kheap_lock, flags);
        return kmalloc(size);
    }

    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    uint64_t flags = spinlock_irqsave(&s_kheap_lock);

    heap_node_t *curr = (heap_node_t *)((uintptr_t)ptr - HEADER_SIZE);
    curr->is_free = 1;

    /* Merge with next adjacent block if free */
    if (curr->next && curr->next->is_free) {
        curr->size += HEADER_SIZE + curr->next->size;
        curr->next = curr->next->next;
        if (curr->next) {
            curr->next->prev = curr;
        }
    }

    /* Merge with previous adjacent block if free */
    if (curr->prev && curr->prev->is_free) {
        curr->prev->size += HEADER_SIZE + curr->size;
        curr->prev->next = curr->next;
        if (curr->next) {
            curr->next->prev = curr->prev;
        }
    }

    spinlock_irqrestore(&s_kheap_lock, flags);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_node_t *node = (heap_node_t *)((uintptr_t)ptr - HEADER_SIZE);
    new_size = ALIGN_UP(new_size, HEAP_ALIGNMENT);

    if (node->size >= new_size) {
        return ptr;
    }

    /* Check if next block is free and merge can avoid a copy */
    if (node->next && node->next->is_free &&
        (node->size + HEADER_SIZE + node->next->size) >= new_size) {
        node->size += HEADER_SIZE + node->next->size;
        node->next = node->next->next;
        if (node->next) {
            node->next->prev = node;
        }
        return ptr;
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, node->size);
    kfree(ptr);
    return new_ptr;
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    if (num != 0 && total / num != size) {
        return NULL; /* Overflow */
    }

    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}