#include "mem/pagecache.h"
#include "mem/pmm.h"
#include "sched/spinlock.h"
#include <string.h>

#define PAGECACHE_BUCKETS 256
#define PAGECACHE_WAYS    2   /* 2-way set-associative = 512 cached pages (2 MiB) */

typedef struct {
    bool      valid;
    uint32_t  inode;
    uint64_t  file_offset;
    uintptr_t phys_frame;
    uint64_t  last_used;
} pagecache_entry_t;

static pagecache_entry_t s_cache[PAGECACHE_BUCKETS][PAGECACHE_WAYS];
static spinlock_t        s_cache_lock = {0};
static uint64_t          s_lru_clock  = 1;

static inline uint32_t cache_hash(uint32_t inode, uint64_t file_offset) {
    uint64_t page_idx = file_offset >> 12;
    uint64_t h = ((uint64_t)inode * 2654435761ULL) ^ (page_idx * 11400714819323198485ULL);
    return (uint32_t)((h >> 32) ^ h) & (PAGECACHE_BUCKETS - 1);
}

void pagecache_init(void) {
    uint64_t flags = spinlock_irqsave(&s_cache_lock);
    memset(s_cache, 0, sizeof(s_cache));
    s_lru_clock = 1;
    spinlock_irqrestore(&s_cache_lock, flags);
}

uintptr_t pagecache_lookup(vfs_node_t *node, uint64_t file_offset) {
    if (!node || !(node->flags & VFS_FILE) || node->inode == 0) {
        return 0;
    }

    uint32_t b = cache_hash(node->inode, file_offset);
    uint64_t flags = spinlock_irqsave(&s_cache_lock);

    for (int w = 0; w < PAGECACHE_WAYS; w++) {
        pagecache_entry_t *e = &s_cache[b][w];
        if (e->valid && e->inode == node->inode && e->file_offset == file_offset) {
            e->last_used = s_lru_clock++;
            uintptr_t phys = e->phys_frame;
            pmm_ref_frame(phys); /* Caller gets a reference for its PTE */
            spinlock_irqrestore(&s_cache_lock, flags);
            return phys;
        }
    }

    spinlock_irqrestore(&s_cache_lock, flags);
    return 0;
}

void pagecache_insert(vfs_node_t *node, uint64_t file_offset, uintptr_t phys_frame) {
    if (!node || !(node->flags & VFS_FILE) || node->inode == 0 || !phys_frame) {
        return;
    }

    uint32_t b = cache_hash(node->inode, file_offset);
    uint64_t flags = spinlock_irqsave(&s_cache_lock);

    /* Check if another thread already inserted this page while we were reading disk */
    for (int w = 0; w < PAGECACHE_WAYS; w++) {
        pagecache_entry_t *e = &s_cache[b][w];
        if (e->valid && e->inode == node->inode && e->file_offset == file_offset) {
            spinlock_irqrestore(&s_cache_lock, flags);
            return;
        }
    }

    /* Pick an empty slot or evict the least-recently-used slot in this bucket */
    int victim = 0;
    uint64_t oldest = UINT64_MAX;
    for (int w = 0; w < PAGECACHE_WAYS; w++) {
        if (!s_cache[b][w].valid) {
            victim = w;
            break;
        }
        if (s_cache[b][w].last_used < oldest) {
            oldest = s_cache[b][w].last_used;
            victim = w;
        }
    }

    pagecache_entry_t *slot = &s_cache[b][victim];
    if (slot->valid && slot->phys_frame) {
        pmm_free_frame(slot->phys_frame); /* Drop cache's reference on evicted frame */
    }

    pmm_ref_frame(phys_frame); /* Cache holds 1 reference to keep the frame alive */
    slot->valid       = true;
    slot->inode       = node->inode;
    slot->file_offset = file_offset;
    slot->phys_frame  = phys_frame;
    slot->last_used   = s_lru_clock++;

    spinlock_irqrestore(&s_cache_lock, flags);
}

void pagecache_invalidate_inode(uint32_t inode) {
    if (inode == 0) return;

    uint64_t flags = spinlock_irqsave(&s_cache_lock);
    for (int b = 0; b < PAGECACHE_BUCKETS; b++) {
        for (int w = 0; w < PAGECACHE_WAYS; w++) {
            pagecache_entry_t *e = &s_cache[b][w];
            if (e->valid && e->inode == inode) {
                if (e->phys_frame) {
                    pmm_free_frame(e->phys_frame);
                }
                e->valid = false;
                e->phys_frame = 0;
            }
        }
    }
    spinlock_irqrestore(&s_cache_lock, flags);
}