#pragma once

#include <stdint.h>
#include "fs/bdev.h"
#include "fs/vfs.h"

#define EXT2_SUPER_MAGIC 0xEF53

#define EXT2_ROOT_INO 2

/* File type constants in directory entries */
#define EXT2_FT_UNKNOWN  0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2
#define EXT2_FT_CHRDEV   3
#define EXT2_FT_BLKDEV   4
#define EXT2_FT_FIFO     5
#define EXT2_FT_SOCK     6
#define EXT2_FT_SYMLINK  7

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block; /* 1 for 1KB blocks, 0 for >1KB */
    uint32_t s_log_block_size;   /* block size = 1024 << s_log_block_size */
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;            /* Must be 0xEF53 */
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;        /* First non-reserved inode (rev >= 1) */
    uint16_t s_inode_size;       /* Size of on-disk inode structure */
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algo_bitmap;
} ext2_superblock_t;

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;     /* Starting block of inode table */
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} ext2_group_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t i_mode;             /* Format and permissions */
    uint16_t i_uid;
    uint32_t i_size;             /* Size in bytes */
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;           /* 512-byte blocks reserved */
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];        /* Pointers to blocks: 0-11 direct, 12 single indirect */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
} ext2_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;            /* Entry length in bytes */
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} ext2_dir_entry_t;

vfs_node_t *ext2_mount(block_device_t *bdev);

#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFLNK  0xA000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFDIR  0x4000