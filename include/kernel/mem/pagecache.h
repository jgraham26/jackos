#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "fs/vfs.h"

void pagecache_init(void);

/*
 * Look up a 4 KiB page for (node->inode, file_offset).
 * On hit, increments the physical frame's PMM refcount and returns its phys addr.
 * Returns 0 on miss.
 */
uintptr_t pagecache_lookup(vfs_node_t *node, uint64_t file_offset);

/*
 * Insert a newly read 4 KiB physical frame into the cache for (node->inode, file_offset).
 * Increments the frame's PMM refcount so the cache retains ownership of 1 reference.
 */
void pagecache_insert(vfs_node_t *node, uint64_t file_offset, uintptr_t phys_frame);

/*
 * Invalidate all cached pages for an inode (call on unlink/truncate/write).
 */
void pagecache_invalidate_inode(uint32_t inode);