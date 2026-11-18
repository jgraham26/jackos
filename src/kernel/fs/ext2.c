#include <stdbool.h>
#include "fs/ext2.h"
#include "fs/vfs.h"
#include "mem/kheap.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>

typedef struct {
    block_device_t    *bdev;
    ext2_superblock_t  sb;
    uint32_t           block_size;
    uint32_t           sectors_per_block;
    uint32_t           block_groups_count;
    ext2_group_desc_t *bgdt;
    uint32_t           inode_size;
} ext2_fs_t;

typedef struct {
    ext2_fs_t   *fs;
    uint32_t     ino;
    ext2_inode_t inode;
} ext2_node_priv_t;

static const vfs_ops_t s_ext2_file_ops;
static const vfs_ops_t s_ext2_dir_ops;

/* Read one filesystem block (handles block sizes 1024, 2048, 4096) */
static int ext2_read_block(ext2_fs_t *fs, uint32_t block_num, void *buf) {
    if (!block_num) {
        memset(buf, 0, fs->block_size);
        return 0;
    }
    uint64_t lba = (uint64_t)block_num * fs->sectors_per_block;
    return bdev_read(fs->bdev, lba, fs->sectors_per_block, buf);
}

/* Map a file's logical block index to its physical block on disk */
static uint32_t ext2_get_file_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical_block) {
    if (logical_block < 12) {
        return inode->i_block[logical_block];
    }

    uint32_t pointers_per_block = fs->block_size / sizeof(uint32_t);
    logical_block -= 12;

    /* Singly Indirect Block */
    if (logical_block < pointers_per_block) {
        if (!inode->i_block[12]) return 0;

        uint32_t *indirect = (uint32_t *)kmalloc(fs->block_size);
        if (!indirect) return 0;
        ext2_read_block(fs, inode->i_block[12], indirect);
        uint32_t phys = indirect[logical_block];
        kfree(indirect);
        return phys;
    }

    logical_block -= pointers_per_block;

    /* Doubly Indirect Block */
    uint64_t dbl_pointers = (uint64_t)pointers_per_block * pointers_per_block;
    if (logical_block < dbl_pointers) {
        if (!inode->i_block[13]) return 0;

        uint32_t *d_indirect = (uint32_t *)kmalloc(fs->block_size);
        if (!d_indirect) return 0;

        if (ext2_read_block(fs, inode->i_block[13], d_indirect) != 0) {
            kfree(d_indirect);
            return 0;
        }

        uint32_t first_idx = logical_block / pointers_per_block;
        uint32_t second_idx = logical_block % pointers_per_block;
        uint32_t second_table_blk = d_indirect[first_idx];
        if (!second_table_blk) {
            kfree(d_indirect);
            return 0;
        }

        uint32_t *leaf = (uint32_t *)kmalloc(fs->block_size);
        if (!leaf) {
            kfree(d_indirect);
            return 0;
        }

        ext2_read_block(fs, second_table_blk, leaf);
        uint32_t phys = leaf[second_idx];

        kfree(leaf);
        kfree(d_indirect);
        return phys;
    }

    return 0;
}

static int ext2_read_inode(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *out) {
    if (ino == 0 || ino > fs->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / fs->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % fs->sb.s_inodes_per_group;

    uint32_t table_block = fs->bgdt[group].bg_inode_table;
    uint32_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint32_t byte_offset  = (index * fs->inode_size) % fs->block_size;

    uint8_t *buf = (uint8_t *)kmalloc(fs->block_size);
    if (!buf) return -1;

    if (ext2_read_block(fs, table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(out, buf + byte_offset, sizeof(ext2_inode_t));
    kfree(buf);
    return 0;
}

static int64_t ext2_readlink(vfs_node_t *node, char *buf, size_t size) {
    if (!node || !node->priv || !buf || size == 0 || !(node->flags & VFS_SYMLINK)) {
        return -1;
    }

    ext2_node_priv_t *priv = (ext2_node_priv_t *)node->priv;
    ext2_fs_t *fs = priv->fs;
    size_t link_len = priv->inode.i_size;
    if (link_len >= size) {
        link_len = size - 1;
    }

    /* Fast symlink: target path (< 60 bytes) is stored inline in i_block[0..14] */
    if (priv->inode.i_size < 60 && priv->inode.i_blocks == 0) {
        memcpy(buf, (const char *)priv->inode.i_block, link_len);
        buf[link_len] = '\0';
        return (int64_t)link_len;
    }

    /* Slow symlink: target path is stored in data block 0 */
    uint8_t *block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!block_buf) return -1;

    uint32_t phys = ext2_get_file_block(fs, &priv->inode, 0);
    if (!phys || ext2_read_block(fs, phys, block_buf) != 0) {
        kfree(block_buf);
        return -1;
    }

    if (link_len > fs->block_size) link_len = fs->block_size;
    memcpy(buf, block_buf, link_len);
    buf[link_len] = '\0';
    kfree(block_buf);
    return (int64_t)link_len;
}

static const vfs_ops_t s_ext2_symlink_ops = {
    .readlink = ext2_readlink,
};

static vfs_node_t *ext2_create_vfs_node(ext2_fs_t *fs, uint32_t ino, const char *name) {
    ext2_inode_t inode;
    if (ext2_read_inode(fs, ino, &inode) != 0) return NULL;

    vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    ext2_node_priv_t *priv = (ext2_node_priv_t *)kmalloc(sizeof(ext2_node_priv_t));
    if (!node || !priv) return NULL;

    memset(node, 0, sizeof(vfs_node_t));
    strncpy(node->name, name, sizeof(node->name) - 1);
    node->inode = ino;
    node->size  = inode.i_size;

    priv->fs    = fs;
    priv->ino   = ino;
    priv->inode = inode;
    node->priv  = priv;

    uint16_t fmt = inode.i_mode & 0xF000;
    if (fmt == 0x4000) {
        node->flags = VFS_DIRECTORY;
        node->ops   = (vfs_ops_t*)&s_ext2_dir_ops;
    } else if (fmt == 0xA000) {
        node->flags = VFS_SYMLINK;
        node->ops = (vfs_ops_t*)&s_ext2_symlink_ops;
    } else {
        node->flags = VFS_FILE;
        node->ops   = (vfs_ops_t*)&s_ext2_file_ops;
    }

    return node;
}

/* --- VFS File Operations --- */

static int64_t ext2_node_read(vfs_file_t *file, void *buf, size_t count) {
    if (!file || !file->node || !file->node->priv || !buf || count == 0) return 0;

    ext2_node_priv_t *priv = (ext2_node_priv_t *)file->node->priv;
    ext2_fs_t *fs = priv->fs;
    uint32_t file_size = priv->inode.i_size;

    if (file->offset >= file_size) return 0;
    if (file->offset + count > file_size) {
        count = file_size - file->offset;
    }

    uint8_t *block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!block_buf) return -1;

    uint8_t *dst = (uint8_t *)buf;
    size_t bytes_read = 0;

    while (bytes_read < count) {
        uint32_t logical_block = file->offset / fs->block_size;
        uint32_t offset_in_block = file->offset % fs->block_size;
        size_t to_copy = fs->block_size - offset_in_block;
        if (to_copy > (count - bytes_read)) to_copy = count - bytes_read;

        uint32_t phys = ext2_get_file_block(fs, &priv->inode, logical_block);
        if (phys == 0) {
            memset(block_buf, 0, fs->block_size);
        } else {
            ext2_read_block(fs, phys, block_buf);
        }

        memcpy(dst + bytes_read, block_buf + offset_in_block, to_copy);
        bytes_read   += to_copy;
        file->offset += to_copy;
    }

    kfree(block_buf);
    return (int64_t)bytes_read;
}

static vfs_node_t *ext2_node_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->priv || !name) return NULL;
    ext2_node_priv_t *priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = priv->fs;

    uint8_t *block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!block_buf) return NULL;

    size_t file_size = priv->inode.i_size;
    uint32_t num_blocks = (file_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_file_block(fs, &priv->inode, b);
        if (!phys) continue;
        ext2_read_block(fs, phys, block_buf);

        uint32_t offset = 0;
        while (offset < fs->block_size && offset < file_size - (b * fs->block_size)) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
            if (!entry->rec_len) break;

            if (entry->inode != 0 && entry->name_len == strlen(name)) {
                if (strncmp(entry->name, name, entry->name_len) == 0) {
                    uint32_t found_ino = entry->inode;
                    kfree(block_buf);
                    vfs_node_t *target = ext2_create_vfs_node(fs, found_ino, name);
                    if (target) target->parent = parent;
                    return target;
                }
            }
            offset += entry->rec_len;
        }
    }

    kfree(block_buf);
    return NULL;
}

static int ext2_node_readdir(vfs_node_t *dir, uint64_t index, vfs_dirent_t *out) {
    if (!dir || !dir->priv || !out) return -1;
    ext2_node_priv_t *priv = (ext2_node_priv_t *)dir->priv;
    ext2_fs_t *fs = priv->fs;

    uint8_t *block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!block_buf) return -1;

    size_t file_size = priv->inode.i_size;
    uint32_t num_blocks = (file_size + fs->block_size - 1) / fs->block_size;
    uint64_t current_idx = 0;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_file_block(fs, &priv->inode, b);
        if (!phys) continue;
        ext2_read_block(fs, phys, block_buf);

        uint32_t offset = 0;
        while (offset < fs->block_size && offset < file_size - (b * fs->block_size)) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
            if (!entry->rec_len) break;

            if (entry->inode != 0) {
                if (current_idx == index) {
                    size_t len = entry->name_len;
                    if (len >= sizeof(out->name)) len = sizeof(out->name) - 1;
                    memcpy(out->name, entry->name, len);
                    out->name[len] = '\0';
                    out->inode = entry->inode;
                    if (entry->file_type == EXT2_FT_DIR) {
                        out->type = VFS_DIRECTORY;
                    } else if (entry->file_type == EXT2_FT_SYMLINK) {
                        out->type = VFS_SYMLINK;
                    } else {
                        out->type = VFS_FILE;
                    }
                    kfree(block_buf);
                    return 0;
                }
                current_idx++;
            }
            offset += entry->rec_len;
        }
    }

    kfree(block_buf);
    return -1;
}

/* Mount Ext2 on top of a block device */
vfs_node_t *ext2_mount(block_device_t *bdev) {
    if (!bdev) return NULL;

    /* Read Superblock (1024 bytes into partition = LBA 2 for 512-byte sectors) */
    uint8_t sb_buf[1024];
    if (bdev_read(bdev, 2, 2, sb_buf) != 0) {
        return NULL;
    }

    ext2_superblock_t *sb = (ext2_superblock_t *)sb_buf;
    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        serial_printf("[EXT2] Bad magic: 0x%04X (expected 0xEF53)\n", sb->s_magic);
        return NULL;
    }

    ext2_fs_t *fs = (ext2_fs_t *)kmalloc(sizeof(ext2_fs_t));
    if (!fs) return NULL;
    memset(fs, 0, sizeof(ext2_fs_t));

    fs->bdev               = bdev;
    memcpy(&fs->sb, sb, sizeof(ext2_superblock_t));
    fs->block_size         = 1024 << sb->s_log_block_size;
    fs->sectors_per_block  = fs->block_size / bdev->block_size;
    fs->inode_size         = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
    fs->block_groups_count = (sb->s_blocks_count + sb->s_blocks_per_group - 1) / sb->s_blocks_per_group;

    /* Read Block Group Descriptor Table */
    uint32_t bgdt_block = (fs->block_size == 1024) ? 2 : 1;
    size_t bgdt_bytes = fs->block_groups_count * sizeof(ext2_group_desc_t);
    size_t bgdt_blocks = (bgdt_bytes + fs->block_size - 1) / fs->block_size;

    fs->bgdt = (ext2_group_desc_t *)kmalloc(bgdt_blocks * fs->block_size);
    if (!fs->bgdt) {
        kfree(fs);
        return NULL;
    }

    for (size_t i = 0; i < bgdt_blocks; i++) {
        ext2_read_block(fs, bgdt_block + i, ((uint8_t *)fs->bgdt) + (i * fs->block_size));
    }

    serial_printf("[EXT2] Mounted volume '%s', block_size=%u, inodes=%u\n",
                  fs->sb.s_volume_name[0] ? fs->sb.s_volume_name : "unnamed",
                  fs->block_size, fs->sb.s_inodes_count);

    /* Inode 2 is the root directory in ext2 */
    vfs_node_t *root_node = ext2_create_vfs_node(fs, EXT2_ROOT_INO, "/");
    if (root_node) {
        root_node->parent = root_node;
    }
    return root_node;
}

/* Write one filesystem block to disk */
static int ext2_write_block(ext2_fs_t *fs, uint32_t block_num, const void *buf) {
    if (!block_num) return -1;
    uint64_t lba = (uint64_t)block_num * fs->sectors_per_block;
    return bdev_write(fs->bdev, lba, fs->sectors_per_block, buf);
}

/* Save the updated Block Group Descriptor Table back to disk */
static int ext2_sync_bgdt(ext2_fs_t *fs) {
    uint32_t bgdt_block = (fs->block_size == 1024) ? 2 : 1;
    size_t bgdt_bytes = fs->block_groups_count * sizeof(ext2_group_desc_t);
    size_t bgdt_blocks = (bgdt_bytes + fs->block_size - 1) / fs->block_size;

    for (size_t i = 0; i < bgdt_blocks; i++) {
        if (ext2_write_block(fs, bgdt_block + i, ((const uint8_t *)fs->bgdt) + (i * fs->block_size)) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Save an inode structure to disk */
static int ext2_write_inode(ext2_fs_t *fs, uint32_t ino, const ext2_inode_t *in) {
    if (ino == 0 || ino > fs->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / fs->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % fs->sb.s_inodes_per_group;

    uint32_t table_block = fs->bgdt[group].bg_inode_table;
    uint32_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint32_t byte_offset  = (index * fs->inode_size) % fs->block_size;

    uint8_t *buf = (uint8_t *)kmalloc(fs->block_size);
    if (!buf) return -1;

    if (ext2_read_block(fs, table_block + block_offset, buf) != 0) {
        kfree(buf);
        return -1;
    }

    memcpy(buf + byte_offset, in, sizeof(ext2_inode_t));

    int res = ext2_write_block(fs, table_block + block_offset, buf);
    kfree(buf);
    return res;
}

/* Allocate a new free data block on the filesystem */
static uint32_t ext2_alloc_block(ext2_fs_t *fs) {
    uint8_t *bitmap = (uint8_t *)kmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < fs->block_groups_count; g++) {
        if (fs->bgdt[g].bg_free_blocks_count == 0) continue;

        uint32_t bitmap_block = fs->bgdt[g].bg_block_bitmap;
        if (ext2_read_block(fs, bitmap_block, bitmap) != 0) continue;

        for (uint32_t b = 0; b < fs->sb.s_blocks_per_group; b++) {
            uint32_t byte_idx = b / 8;
            uint8_t bit_mask = 1 << (b % 8);

            if (!(bitmap[byte_idx] & bit_mask)) {
                /* Mark as used */
                bitmap[byte_idx] |= bit_mask;
                ext2_write_block(fs, bitmap_block, bitmap);

                /* Zero the allocated data block */
                uint32_t block_num = (g * fs->sb.s_blocks_per_group) + b + fs->sb.s_first_data_block;
                uint8_t *zero_buf = (uint8_t *)kmalloc(fs->block_size);
                if (zero_buf) {
                    memset(zero_buf, 0, fs->block_size);
                    ext2_write_block(fs, block_num, zero_buf);
                    kfree(zero_buf);
                }

                fs->bgdt[g].bg_free_blocks_count--;
                fs->sb.s_free_blocks_count--;
                ext2_sync_bgdt(fs);

                kfree(bitmap);
                return block_num;
            }
        }
    }

    kfree(bitmap);
    return 0; /* Disk full */
}

/* Allocate a new inode */
static uint32_t ext2_alloc_inode(ext2_fs_t *fs, int is_dir) {
    uint8_t *bitmap = (uint8_t *)kmalloc(fs->block_size);
    if (!bitmap) return 0;

    for (uint32_t g = 0; g < fs->block_groups_count; g++) {
        if (fs->bgdt[g].bg_free_inodes_count == 0) continue;

        uint32_t bitmap_block = fs->bgdt[g].bg_inode_bitmap;
        if (ext2_read_block(fs, bitmap_block, bitmap) != 0) continue;

        for (uint32_t i = 0; i < fs->sb.s_inodes_per_group; i++) {
            uint32_t byte_idx = i / 8;
            uint8_t bit_mask = 1 << (i % 8);

            if (!(bitmap[byte_idx] & bit_mask)) {
                bitmap[byte_idx] |= bit_mask;
                ext2_write_block(fs, bitmap_block, bitmap);

                fs->bgdt[g].bg_free_inodes_count--;
                if (is_dir) {
                    fs->bgdt[g].bg_used_dirs_count++;
                }
                fs->sb.s_free_inodes_count--;
                ext2_sync_bgdt(fs);

                kfree(bitmap);
                return (g * fs->sb.s_inodes_per_group) + i + 1;
            }
        }
    }

    kfree(bitmap);
    return 0;
}

static uint32_t ext2_get_or_alloc_file_block(ext2_fs_t *fs, ext2_inode_t *inode, uint32_t logical_block) {
    /* 1. Direct blocks (0..11) */
    if (logical_block < 12) {
        if (inode->i_block[logical_block] == 0) {
            uint32_t blk = ext2_alloc_block(fs);
            if (!blk) return 0;
            inode->i_block[logical_block] = blk;
            inode->i_blocks += (fs->block_size / 512);
        }
        return inode->i_block[logical_block];
    }

    uint32_t pointers_per_block = fs->block_size / sizeof(uint32_t);
    logical_block -= 12;

    /* 2. Singly Indirect Block */
    if (logical_block < pointers_per_block) {
        if (inode->i_block[12] == 0) {
            uint32_t indirect_table = ext2_alloc_block(fs);
            if (!indirect_table) return 0;
            inode->i_block[12] = indirect_table;
            inode->i_blocks += (fs->block_size / 512);
        }

        uint32_t *indirect = (uint32_t *)kmalloc(fs->block_size);
        if (!indirect) return 0;

        ext2_read_block(fs, inode->i_block[12], indirect);

        if (indirect[logical_block] == 0) {
            uint32_t blk = ext2_alloc_block(fs);
            if (!blk) {
                kfree(indirect);
                return 0;
            }
            indirect[logical_block] = blk;
            inode->i_blocks += (fs->block_size / 512);
            ext2_write_block(fs, inode->i_block[12], indirect);
        }

        uint32_t phys = indirect[logical_block];
        kfree(indirect);
        return phys;
    }

    return 0; /* Exceeds single indirect limit */
}

static int64_t ext2_node_write(vfs_file_t *file, const void *buf, size_t count) {
    if (!file || !file->node || !file->node->priv || !buf || count == 0) return 0;

    ext2_node_priv_t *priv = (ext2_node_priv_t *)file->node->priv;
    ext2_fs_t *fs = priv->fs;
    const uint8_t *src = (const uint8_t *)buf;

    uint8_t *block_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!block_buf) return -1;

    size_t bytes_written = 0;

    while (bytes_written < count) {
        uint32_t logical_block = file->offset / fs->block_size;
        uint32_t offset_in_block = file->offset % fs->block_size;
        size_t to_copy = fs->block_size - offset_in_block;
        if (to_copy > (count - bytes_written)) {
            to_copy = count - bytes_written;
        }

        uint32_t phys = ext2_get_or_alloc_file_block(fs, &priv->inode, logical_block);
        if (phys == 0) {
            kfree(block_buf);
            return bytes_written > 0 ? (int64_t)bytes_written : -1;
        }

        /* Read-modify-write if partial block overwrite */
        if (offset_in_block != 0 || to_copy < fs->block_size) {
            ext2_read_block(fs, phys, block_buf);
        }

        memcpy(block_buf + offset_in_block, src + bytes_written, to_copy);
        ext2_write_block(fs, phys, block_buf);

        bytes_written += to_copy;
        file->offset += to_copy;

        if (file->offset > priv->inode.i_size) {
            priv->inode.i_size = (uint32_t)file->offset;
            file->node->size = priv->inode.i_size;
        }
    }

    /* Update on-disk inode metadata (size, blocks) */
    ext2_write_inode(fs, priv->ino, &priv->inode);

    kfree(block_buf);
    return (int64_t)bytes_written;
}

static const vfs_ops_t s_ext2_file_ops = {
    .read = ext2_node_read,
    .write = ext2_node_write
};

#define EXT2_DIR_REC_LEN(name_len) (((sizeof(ext2_dir_entry_t) + (name_len) + 3) & ~3))

static int ext2_add_dir_entry(ext2_fs_t *fs, ext2_inode_t *dir_inode, uint32_t dir_ino,
                              uint32_t new_ino, const char *name, uint8_t file_type) {
    size_t name_len = strlen(name);
    uint16_t needed_len = EXT2_DIR_REC_LEN(name_len);

    uint8_t *buf = (uint8_t *)kmalloc(fs->block_size);
    if (!buf) return -1;

    size_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_file_block(fs, dir_inode, b);
        if (!phys) continue;

        ext2_read_block(fs, phys, buf);
        uint32_t offset = 0;

        while (offset < fs->block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + offset);
            uint16_t actual_used = EXT2_DIR_REC_LEN(entry->name_len);

            /* Check if entry has excess trailing padding that can accommodate the new record */
            if (entry->rec_len >= actual_used + needed_len) {
                uint16_t old_rec_len = entry->rec_len;
                entry->rec_len = actual_used;

                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(buf + offset + actual_used);
                new_entry->inode = new_ino;
                new_entry->rec_len = old_rec_len - actual_used;
                new_entry->name_len = (uint8_t)name_len;
                new_entry->file_type = file_type;
                memcpy(new_entry->name, name, name_len);

                ext2_write_block(fs, phys, buf);
                kfree(buf);
                return 0;
            }

            offset += entry->rec_len;
        }
    }

    /* If no space in existing blocks, allocate a new block for the directory */
    uint32_t new_block = ext2_alloc_block(fs);
    if (!new_block) {
        kfree(buf);
        return -1;
    }

    memset(buf, 0, fs->block_size);
    ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)buf;
    new_entry->inode = new_ino;
    new_entry->rec_len = (uint16_t)fs->block_size;
    new_entry->name_len = (uint8_t)name_len;
    new_entry->file_type = file_type;
    memcpy(new_entry->name, name, name_len);

    uint32_t logical_block = dir_inode->i_size / fs->block_size;
    if (logical_block < 12) {
        dir_inode->i_block[logical_block] = new_block;
        dir_inode->i_blocks += (fs->block_size / 512);
    }
    dir_inode->i_size += fs->block_size;

    ext2_write_block(fs, new_block, buf);
    ext2_write_inode(fs, dir_ino, dir_inode);

    kfree(buf);
    return 0;
}

static int ext2_node_symlink(vfs_node_t *parent, const char *name, const char *target) {
    if (!parent || !parent->priv || !name || !target) return -1;

    ext2_node_priv_t *parent_priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = parent_priv->fs;

    size_t target_len = strlen(target);
    if (target_len == 0 || target_len >= fs->block_size) {
        return -1;
    }

    uint32_t ino = ext2_alloc_inode(fs, 0);
    if (!ino) return -1;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode        = 0xA1FF; /* S_IFLNK (0xA000) | 0777 */
    new_inode.i_links_count = 1;
    new_inode.i_size        = (uint32_t)target_len;

    if (target_len < 60) {
        /* Fast symlink: store target string directly inside i_block[0..14] */
        memcpy((char *)new_inode.i_block, target, target_len);
        new_inode.i_blocks = 0;
    } else {
        /* Slow symlink: allocate one data block and write target string */
        uint32_t blk = ext2_alloc_block(fs);
        if (!blk) return -1;

        uint8_t *blk_buf = (uint8_t *)kmalloc(fs->block_size);
        if (!blk_buf) return -1;

        memset(blk_buf, 0, fs->block_size);
        memcpy(blk_buf, target, target_len);
        ext2_write_block(fs, blk, blk_buf);
        kfree(blk_buf);

        new_inode.i_block[0] = blk;
        new_inode.i_blocks   = fs->block_size / 512;
    }

    if (ext2_write_inode(fs, ino, &new_inode) != 0) {
        return -1;
    }

    if (ext2_add_dir_entry(fs, &parent_priv->inode, parent_priv->ino, ino, name, EXT2_FT_SYMLINK) != 0) {
        return -1;
    }

    /* Persist updated parent directory inode size/blocks if a new dir block was allocated */
    ext2_write_inode(fs, parent_priv->ino, &parent_priv->inode);
    return 0;
}

static vfs_node_t *ext2_node_create(vfs_node_t *parent, const char *name, uint32_t mode) {
    serial_printf("[ext2_create] Creating '%s' in parent inode %u\n", name, parent->inode);
    if (!parent || !parent->priv || !name) return NULL;
    ext2_node_priv_t *parent_priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = parent_priv->fs;

    uint32_t ino = ext2_alloc_inode(fs, 0);
    serial_printf("[ext2_create] ext2_alloc_inode returned ino=%u\n", ino);
    if (!ino) return NULL;

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode = 0x81A4; /* S_IFREG | 0644 */
    new_inode.i_links_count = 1;
    new_inode.i_size = 0;

    ext2_write_inode(fs, ino, &new_inode);

    int dir_res = ext2_add_dir_entry(fs, &parent_priv->inode, parent_priv->ino, ino, name, EXT2_FT_REG_FILE);
    serial_printf("[ext2_create] ext2_add_dir_entry returned %d\n", dir_res);
    if (dir_res != 0) {
        return NULL;
    }

    vfs_node_t *node = ext2_create_vfs_node(fs, ino, name);
    if (node) {
        node->parent = parent;
    }
    return node;
}

/* Free a single block back to its block group bitmap */
static void ext2_free_block(ext2_fs_t *fs, uint32_t block_num) {
    if (block_num < fs->sb.s_first_data_block || block_num >= fs->sb.s_blocks_count) return;

    uint32_t rel = block_num - fs->sb.s_first_data_block;
    uint32_t g   = rel / fs->sb.s_blocks_per_group;
    uint32_t b   = rel % fs->sb.s_blocks_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(fs->block_size);
    if (!bitmap) return;

    uint32_t bitmap_block = fs->bgdt[g].bg_block_bitmap;
    if (ext2_read_block(fs, bitmap_block, bitmap) == 0) {
        uint32_t byte_idx = b / 8;
        uint8_t  bit_mask = 1 << (b % 8);
        if (bitmap[byte_idx] & bit_mask) {
            bitmap[byte_idx] &= ~bit_mask;
            ext2_write_block(fs, bitmap_block, bitmap);
            fs->bgdt[g].bg_free_blocks_count++;
            fs->sb.s_free_blocks_count++;
            ext2_sync_bgdt(fs);
        }
    }
    kfree(bitmap);
}

/* Free an inode's data blocks (direct + singly indirect) and clear its inode bitmap bit */
static void ext2_free_inode_and_blocks(ext2_fs_t *fs, uint32_t ino, ext2_inode_t *inode, bool is_dir) {
    /* Only free data blocks if not a fast inline symlink */
    bool is_fast_symlink = (((inode->i_mode & 0xF000) == 0xA000) && inode->i_size < 60 && inode->i_blocks == 0);

    if (!is_fast_symlink) {
        for (int i = 0; i < 12; i++) {
            if (inode->i_block[i]) {
                ext2_free_block(fs, inode->i_block[i]);
                inode->i_block[i] = 0;
            }
        }
        if (inode->i_block[12]) {
            uint32_t *ind = (uint32_t *)kmalloc(fs->block_size);
            if (ind) {
                if (ext2_read_block(fs, inode->i_block[12], ind) == 0) {
                    uint32_t ptrs = fs->block_size / sizeof(uint32_t);
                    for (uint32_t i = 0; i < ptrs; i++) {
                        if (ind[i]) ext2_free_block(fs, ind[i]);
                    }
                }
                kfree(ind);
            }
            ext2_free_block(fs, inode->i_block[12]);
            inode->i_block[12] = 0;
        }
    }

    memset(inode, 0, sizeof(ext2_inode_t));
    ext2_write_inode(fs, ino, inode);

    uint32_t g = (ino - 1) / fs->sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % fs->sb.s_inodes_per_group;

    uint8_t *bitmap = (uint8_t *)kmalloc(fs->block_size);
    if (!bitmap) return;

    uint32_t bitmap_block = fs->bgdt[g].bg_inode_bitmap;
    if (ext2_read_block(fs, bitmap_block, bitmap) == 0) {
        uint32_t byte_idx = idx / 8;
        uint8_t  bit_mask = 1 << (idx % 8);
        if (bitmap[byte_idx] & bit_mask) {
            bitmap[byte_idx] &= ~bit_mask;
            ext2_write_block(fs, bitmap_block, bitmap);
            fs->bgdt[g].bg_free_inodes_count++;
            if (is_dir && fs->bgdt[g].bg_used_dirs_count > 0) {
                fs->bgdt[g].bg_used_dirs_count--;
            }
            fs->sb.s_free_inodes_count++;
            ext2_sync_bgdt(fs);
        }
    }
    kfree(bitmap);
}

/* Remove a directory entry by name, optionally returning its inode number */
static int ext2_remove_dir_entry(ext2_fs_t *fs, ext2_inode_t *dir_inode, const char *name, uint32_t *out_ino) {
    size_t name_len = strlen(name);
    uint8_t *buf = (uint8_t *)kmalloc(fs->block_size);
    if (!buf) return -ENOMEM;

    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_file_block(fs, dir_inode, b);
        if (!phys) continue;

        ext2_read_block(fs, phys, buf);
        uint32_t offset = 0;
        ext2_dir_entry_t *prev = NULL;

        while (offset < fs->block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + offset);
            if (!entry->rec_len) break;

            if (entry->inode != 0 && entry->name_len == name_len &&
                strncmp(entry->name, name, name_len) == 0) {
                if (out_ino) *out_ino = entry->inode;

                if (prev) {
                    /* Merge current entry's length into previous entry */
                    prev->rec_len += entry->rec_len;
                } else {
                    /* First entry in block: mark unused by zeroing inode */
                    entry->inode = 0;
                }

                ext2_write_block(fs, phys, buf);
                kfree(buf);
                return 0;
            }

            prev = entry;
            offset += entry->rec_len;
        }
    }

    kfree(buf);
    return -ENOENT;
}

/* Check whether a directory contains any entries besides "." and ".." */
static bool ext2_dir_is_empty(ext2_fs_t *fs, ext2_inode_t *dir_inode) {
    uint8_t *buf = (uint8_t *)kmalloc(fs->block_size);
    if (!buf) return false;

    uint32_t num_blocks = (dir_inode->i_size + fs->block_size - 1) / fs->block_size;

    for (uint32_t b = 0; b < num_blocks; b++) {
        uint32_t phys = ext2_get_file_block(fs, dir_inode, b);
        if (!phys) continue;

        ext2_read_block(fs, phys, buf);
        uint32_t offset = 0;

        while (offset < fs->block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(buf + offset);
            if (!entry->rec_len) break;

            if (entry->inode != 0) {
                bool is_dot = (entry->name_len == 1 && entry->name[0] == '.');
                bool is_dotdot = (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.');
                if (!is_dot && !is_dotdot) {
                    kfree(buf);
                    return false;
                }
            }
            offset += entry->rec_len;
        }
    }

    kfree(buf);
    return true;
}

static int ext2_node_mkdir(vfs_node_t *parent, const char *name, uint32_t mode) {
    if (!parent || !parent->priv || !name) return -EINVAL;
    ext2_node_priv_t *parent_priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = parent_priv->fs;

    uint32_t ino = ext2_alloc_inode(fs, 1);
    if (!ino) return -ENOSPC;

    uint32_t blk = ext2_alloc_block(fs);
    if (!blk) return -ENOSPC;

    /* Initialize directory block with "." and ".." */
    uint8_t *dir_buf = (uint8_t *)kmalloc(fs->block_size);
    if (!dir_buf) return -ENOMEM;
    memset(dir_buf, 0, fs->block_size);

    ext2_dir_entry_t *dot = (ext2_dir_entry_t *)dir_buf;
    dot->inode     = ino;
    dot->rec_len   = EXT2_DIR_REC_LEN(1);
    dot->name_len  = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0]   = '.';

    ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(dir_buf + dot->rec_len);
    dotdot->inode     = parent_priv->ino;
    dotdot->rec_len   = (uint16_t)(fs->block_size - dot->rec_len);
    dotdot->name_len  = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0]   = '.';
    dotdot->name[1]   = '.';

    ext2_write_block(fs, blk, dir_buf);
    kfree(dir_buf);

    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(ext2_inode_t));
    new_inode.i_mode        = 0x4000 | (mode & 0777 ? (mode & 0777) : 0755);
    new_inode.i_links_count = 2; /* "." + parent entry */
    new_inode.i_size        = fs->block_size;
    new_inode.i_blocks      = fs->block_size / 512;
    new_inode.i_block[0]    = blk;

    ext2_write_inode(fs, ino, &new_inode);

    if (ext2_add_dir_entry(fs, &parent_priv->inode, parent_priv->ino, ino, name, EXT2_FT_DIR) != 0) {
        return -ENOSPC;
    }

    /* Increment parent's link count for child's ".." */
    parent_priv->inode.i_links_count++;
    ext2_write_inode(fs, parent_priv->ino, &parent_priv->inode);
    return 0;
}

static int ext2_node_unlink(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->priv || !name) return -EINVAL;
    ext2_node_priv_t *parent_priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = parent_priv->fs;

    vfs_node_t *target = ext2_node_lookup(parent, name);
    if (!target) return -ENOENT;
    if (target->flags & VFS_DIRECTORY) {
        kfree(target->priv);
        kfree(target);
        return -EISDIR;
    }

    ext2_node_priv_t *tpriv = (ext2_node_priv_t *)target->priv;
    uint32_t target_ino = tpriv->ino;
    ext2_inode_t target_inode = tpriv->inode;
    kfree(tpriv);
    kfree(target);

    int err = ext2_remove_dir_entry(fs, &parent_priv->inode, name, NULL);
    if (err != 0) return err;

    if (target_inode.i_links_count > 0) {
        target_inode.i_links_count--;
    }

    if (target_inode.i_links_count == 0) {
        ext2_free_inode_and_blocks(fs, target_ino, &target_inode, false);
    } else {
        ext2_write_inode(fs, target_ino, &target_inode);
    }

    return 0;
}

static int ext2_node_rmdir(vfs_node_t *parent, const char *name) {
    if (!parent || !parent->priv || !name) return -EINVAL;
    ext2_node_priv_t *parent_priv = (ext2_node_priv_t *)parent->priv;
    ext2_fs_t *fs = parent_priv->fs;

    vfs_node_t *target = ext2_node_lookup(parent, name);
    if (!target) return -ENOENT;
    if (!(target->flags & VFS_DIRECTORY)) {
        kfree(target->priv);
        kfree(target);
        return -ENOTDIR;
    }

    ext2_node_priv_t *tpriv = (ext2_node_priv_t *)target->priv;
    uint32_t target_ino = tpriv->ino;
    ext2_inode_t target_inode = tpriv->inode;
    kfree(tpriv);
    kfree(target);

    if (!ext2_dir_is_empty(fs, &target_inode)) {
        return -ENOTEMPTY;
    }

    int err = ext2_remove_dir_entry(fs, &parent_priv->inode, name, NULL);
    if (err != 0) return err;

    ext2_free_inode_and_blocks(fs, target_ino, &target_inode, true);

    if (parent_priv->inode.i_links_count > 1) {
        parent_priv->inode.i_links_count--;
    }
    ext2_write_inode(fs, parent_priv->ino, &parent_priv->inode);
    return 0;
}

static int ext2_node_rename(vfs_node_t *old_parent, const char *old_name,
                            vfs_node_t *new_parent, const char *new_name) {
    if (!old_parent || !old_parent->priv || !new_parent || !new_parent->priv) return -EINVAL;
    ext2_node_priv_t *old_ppriv = (ext2_node_priv_t *)old_parent->priv;
    ext2_node_priv_t *new_ppriv = (ext2_node_priv_t *)new_parent->priv;
    ext2_fs_t *fs = old_ppriv->fs;

    vfs_node_t *src = ext2_node_lookup(old_parent, old_name);
    if (!src) return -ENOENT;

    ext2_node_priv_t *src_priv = (ext2_node_priv_t *)src->priv;
    uint32_t src_ino = src_priv->ino;
    bool src_is_dir = (src->flags & VFS_DIRECTORY) != 0;
    uint8_t file_type = src_is_dir ? EXT2_FT_DIR :
                        ((src->flags & VFS_SYMLINK) ? EXT2_FT_SYMLINK : EXT2_FT_REG_FILE);
    kfree(src_priv);
    kfree(src);

    /* If moving to the exact same directory & filename, it's a POSIX no-op */
    if (old_ppriv->ino == new_ppriv->ino && strcmp(old_name, new_name) == 0) {
        return 0;
    }

    /* If destination exists, remove it first (POSIX atomic replace semantics) */
    vfs_node_t *dst = ext2_node_lookup(new_parent, new_name);
    if (dst) {
        bool dst_is_dir = (dst->flags & VFS_DIRECTORY) != 0;
        kfree(dst->priv);
        kfree(dst);

        if (src_is_dir && !dst_is_dir) return -ENOTDIR;
        if (!src_is_dir && dst_is_dir) return -EISDIR;

        int rm_err = dst_is_dir ? ext2_node_rmdir(new_parent, new_name)
                                : ext2_node_unlink(new_parent, new_name);
        if (rm_err != 0) return rm_err;
    }

    if (ext2_add_dir_entry(fs, &new_ppriv->inode, new_ppriv->ino, src_ino, new_name, file_type) != 0) {
        return -ENOSPC;
    }
    ext2_write_inode(fs, new_ppriv->ino, &new_ppriv->inode);

    ext2_remove_dir_entry(fs, &old_ppriv->inode, old_name, NULL);

    /* If a directory moved across different parents, update its ".." entry and parent link counts */
    if (src_is_dir && old_ppriv->ino != new_ppriv->ino) {
        ext2_inode_t moved_dir;
        if (ext2_read_inode(fs, src_ino, &moved_dir) == 0 && moved_dir.i_block[0]) {
            uint8_t *blk_buf = (uint8_t *)kmalloc(fs->block_size);
            if (blk_buf && ext2_read_block(fs, moved_dir.i_block[0], blk_buf) == 0) {
                ext2_dir_entry_t *dot = (ext2_dir_entry_t *)blk_buf;
                ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(blk_buf + dot->rec_len);
                if (dotdot->name_len == 2 && dotdot->name[0] == '.' && dotdot->name[1] == '.') {
                    dotdot->inode = new_ppriv->ino;
                    ext2_write_block(fs, moved_dir.i_block[0], blk_buf);
                }
            }
            if (blk_buf) kfree(blk_buf);
        }

        if (old_ppriv->inode.i_links_count > 1) old_ppriv->inode.i_links_count--;
        new_ppriv->inode.i_links_count++;
        ext2_write_inode(fs, old_ppriv->ino, &old_ppriv->inode);
        ext2_write_inode(fs, new_ppriv->ino, &new_ppriv->inode);
    }

    return 0;
}

static int ext2_node_truncate(vfs_node_t *node, uint64_t length) {
    if (!node || !node->priv) return -EINVAL;
    if (node->flags & VFS_DIRECTORY) return -EISDIR;

    ext2_node_priv_t *priv = (ext2_node_priv_t *)node->priv;
    ext2_fs_t *fs = priv->fs;

    if (length == 0) {
        for (int i = 0; i < 12; i++) {
            if (priv->inode.i_block[i]) {
                ext2_free_block(fs, priv->inode.i_block[i]);
                priv->inode.i_block[i] = 0;
            }
        }
        if (priv->inode.i_block[12]) {
            uint32_t *ind = (uint32_t *)kmalloc(fs->block_size);
            if (ind) {
                if (ext2_read_block(fs, priv->inode.i_block[12], ind) == 0) {
                    uint32_t ptrs = fs->block_size / sizeof(uint32_t);
                    for (uint32_t i = 0; i < ptrs; i++) {
                        if (ind[i]) ext2_free_block(fs, ind[i]);
                    }
                }
                kfree(ind);
            }
            ext2_free_block(fs, priv->inode.i_block[12]);
            priv->inode.i_block[12] = 0;
        }
        priv->inode.i_blocks = 0;
    }

    priv->inode.i_size = (uint32_t)length;
    node->size = length;
    ext2_write_inode(fs, priv->ino, &priv->inode);
    return 0;
}

static const vfs_ops_t s_ext2_dir_ops = {
    .open     = NULL,
    .close    = NULL,
    .read     = NULL,
    .write    = NULL,
    .lookup   = ext2_node_lookup,
    .readdir  = ext2_node_readdir,
    .create   = ext2_node_create,
    .mkdir    = ext2_node_mkdir,
    .symlink  = ext2_node_symlink,
    .unlink   = ext2_node_unlink,
    .rmdir    = ext2_node_rmdir,
    .rename   = ext2_node_rename,
    .truncate = ext2_node_truncate,
};