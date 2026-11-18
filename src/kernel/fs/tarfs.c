#include "fs/vfs.h"
#include "mem/kheap.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];     /* Octal string */
    char mtime[12];
    char chksum[8];
    char typeflag;     /* '0' = normal file, '5' = directory */
    char linkname[100];
    char magic[6];     /* "ustar\0" */
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} __attribute__((packed));

static vfs_ops_t s_tarfs_ops;

typedef struct tarfs_entry {
    char name[VFS_MAX_NAME];
    uint8_t *data;
    size_t size;
    bool is_dir;
    struct tarfs_entry *next;
} tarfs_entry_t;

static tarfs_entry_t *s_tar_entries = NULL;

static size_t parse_octal(const char *str, size_t max_len) {
    size_t val = 0;
    for (size_t i = 0; i < max_len && str[i] >= '0' && str[i] <= '7'; i++) {
        val = (val << 3) + (str[i] - '0');
    }
    return val;
}

static int64_t tarfs_read(vfs_file_t *file, void *buf, size_t count) {
    tarfs_entry_t *entry = (tarfs_entry_t *)file->node->device_data;
    if (!entry || entry->is_dir) return -1;

    if (file->offset >= entry->size) {
        return 0; /* EOF */
    }

    if (file->offset + count > entry->size) {
        count = entry->size - file->offset;
    }

    memcpy(buf, entry->data + file->offset, count);
    file->offset += count;
    return (int64_t)count;
}

vfs_node_t *tarfs_init(uintptr_t archive_start, size_t archive_size) {
    uint8_t *ptr = (uint8_t *)archive_start;
    uint8_t *end = ptr + archive_size;

    while (ptr + 512 <= end) {
        struct tar_header *hdr = (struct tar_header *)ptr;
        if (hdr->name[0] == '\0') break;

        size_t file_size = parse_octal(hdr->size, sizeof(hdr->size));
        uint8_t *file_data = ptr + 512;

        const char *raw_name = hdr->name;

        /* 1. Strip leading "./" if present */
        while (raw_name[0] == '.' && raw_name[1] == '/') {
            raw_name += 2;
        }

        /* 2. Strip leading '/' if present */
        while (raw_name[0] == '/') {
            raw_name++;
        }

        /* Skip the root '.' entry itself */
        if (raw_name[0] == '\0' || strcmp(raw_name, ".") == 0) {
            ptr += 512 + ((file_size + 511) & ~511);
            continue;
        }

        /* Strip trailing slash for directories */
        char clean_name[VFS_MAX_NAME];
        strncpy(clean_name, raw_name, sizeof(clean_name) - 1);
        clean_name[sizeof(clean_name) - 1] = '\0';
        
        size_t len = strlen(clean_name);
        if (len > 0 && clean_name[len - 1] == '/') {
            clean_name[len - 1] = '\0';
        }

        tarfs_entry_t *entry = (tarfs_entry_t *)kmalloc(sizeof(tarfs_entry_t));
        strncpy(entry->name, clean_name, sizeof(entry->name) - 1);
        entry->name[sizeof(entry->name) - 1] = '\0';
        entry->size = file_size;
        entry->data = file_data;
        entry->is_dir = (hdr->typeflag == '5' || (len > 0 && hdr->name[len - 1] == '/'));
        entry->next = s_tar_entries;
        s_tar_entries = entry;

        ptr += 512 + ((file_size + 511) & ~511);
    }

    vfs_node_t *root = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
    memset(root, 0, sizeof(vfs_node_t));
    strcpy(root->name, "/");
    root->flags = VFS_DIRECTORY;
    root->ops = &s_tarfs_ops;
    root->parent = root;
    return root;
}

static int tarfs_readdir(vfs_node_t *dir, size_t index, vfs_dirent_t *out_entry) {
    if (!dir || !out_entry) return -1;
    if (!(dir->flags & VFS_DIRECTORY)) return -1;

    /* Entry 0: "." */
    if (index == 0) {
        strcpy(out_entry->name, ".");
        out_entry->inode = dir->inode ? dir->inode : 1;
        out_entry->type = VFS_DIRECTORY;
        return 0;
    }

    /* Entry 1: ".." */
    if (index == 1) {
        strcpy(out_entry->name, "..");
        out_entry->inode = (dir->parent && dir->parent->inode) ? dir->parent->inode : 1;
        out_entry->type = VFS_DIRECTORY;
        return 0;
    }

    size_t target_idx = index - 2;
    size_t current_idx = 0;

    /* 1. Build directory prefix (e.g. "" for root "/", or "bin/" for "/bin") */
    char prefix[VFS_MAX_PATH];
    prefix[0] = '\0';

    if (strcmp(dir->name, "/") != 0 && strcmp(dir->name, "") != 0) {
        size_t dlen = strlen(dir->name);
        strncpy(prefix, dir->name, sizeof(prefix) - 2);
        prefix[sizeof(prefix) - 2] = '\0';
        
        if (dlen > 0 && prefix[dlen - 1] != '/') {
            prefix[dlen] = '/';
            prefix[dlen + 1] = '\0';
        }
    }
    size_t prefix_len = strlen(prefix);

    #define MAX_SEEN_DIRS 64
    char seen_dirs[MAX_SEEN_DIRS][VFS_MAX_NAME];
    size_t seen_count = 0;

    for (tarfs_entry_t *e = s_tar_entries; e; e = e->next) {
        const char *name = e->name;

        while (*name == '/') {
            name++;
        }

        /* Must start with parent prefix */
        if (prefix_len > 0) {
            if (strncmp(name, prefix, prefix_len) != 0) {
                continue;
            }
        }

        const char *rel_path = name + prefix_len;
        if (*rel_path == '\0' || strcmp(rel_path, ".") == 0) {
            continue;
        }

        /* Copy relative path and strip trailing slash if it is an explicit dir */
        char clean_rel[VFS_MAX_NAME];
        strncpy(clean_rel, rel_path, sizeof(clean_rel) - 1);
        clean_rel[sizeof(clean_rel) - 1] = '\0';
        size_t rlen = strlen(clean_rel);
        if (rlen > 0 && clean_rel[rlen - 1] == '/') {
            clean_rel[--rlen] = '\0';
        }

        if (rlen == 0) continue;

        char item_name[VFS_MAX_NAME];
        uint32_t item_type = VFS_FILE;

        /* Look for an internal slash separating subdirectories */
        char *slash = strchr(clean_rel, '/');
        if (slash != NULL) {
            /* It is inside a subdirectory -> extract just the first component */
            size_t comp_len = (size_t)(slash - clean_rel);
            if (comp_len >= sizeof(item_name)) {
                comp_len = sizeof(item_name) - 1;
            }
            memcpy(item_name, clean_rel, comp_len);
            item_name[comp_len] = '\0';
            item_type = VFS_DIRECTORY;
        } else {
            /* Immediate child */
            strncpy(item_name, clean_rel, sizeof(item_name) - 1);
            item_name[sizeof(item_name) - 1] = '\0';
            item_type = e->is_dir ? VFS_DIRECTORY : VFS_FILE;
        }

        /* De-duplicate names (handles both explicit dir header and nested files) */
        bool duplicate = false;
        for (size_t s = 0; s < seen_count; s++) {
            if (strcmp(seen_dirs[s], item_name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        if (seen_count < MAX_SEEN_DIRS) {
            strncpy(seen_dirs[seen_count], item_name, VFS_MAX_NAME - 1);
            seen_dirs[seen_count][VFS_MAX_NAME - 1] = '\0';
            seen_count++;
        }

        /* Match the caller's target index */
        if (current_idx == target_idx) {
            strncpy(out_entry->name, item_name, VFS_MAX_NAME - 1);
            out_entry->name[VFS_MAX_NAME - 1] = '\0';
            out_entry->inode = (uint32_t)(current_idx + 2);
            out_entry->type = item_type;
            return 0;
        }

        current_idx++;
    }

    return -1; /* End of directory */
}

static vfs_ops_t s_tarfs_ops; /* Forward declaration */

static vfs_node_t *tarfs_lookup(vfs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;

    if (strcmp(name, ".") == 0) return parent;
    if (strcmp(name, "..") == 0) return parent->parent ? parent->parent : parent;

    /* Strip any leading slash */
    if (name[0] == '/') {
        name++;
    }

    /* Build the target string to match against tar entries:
     * If parent is root ("/"), look for "name" (e.g. "bin")
     * If parent is a subfolder, look for "<parent_full_path>/<name>" (e.g. "bin/init")
     */
    char target_path[VFS_MAX_PATH];
    target_path[0] = '\0';

    if (strcmp(parent->name, "/") == 0) {
        strncpy(target_path, name, sizeof(target_path) - 1);
        target_path[sizeof(target_path) - 1] = '\0';
    } else {
        tarfs_entry_t *p_entry = (tarfs_entry_t *)parent->device_data;
        const char *parent_prefix = p_entry ? p_entry->name : parent->name;

        size_t len = 0;
        while (parent_prefix[len] && len < sizeof(target_path) - 2) {
            target_path[len] = parent_prefix[len];
            len++;
        }
        target_path[len++] = '/';

        size_t nlen = 0;
        while (name[nlen] && (len + nlen) < sizeof(target_path) - 1) {
            target_path[len + nlen] = name[nlen];
            nlen++;
        }
        target_path[len + nlen] = '\0';
    }

    serial_printf("[TARFS] Lookup parent='%s', looking for '%s' -> target='%s'\n",
              parent->name, name, target_path);

    /* Scan entries for target_path */
    for (tarfs_entry_t *e = s_tar_entries; e; e = e->next) {
        if (strcmp(e->name, target_path) == 0) {
            vfs_node_t *node = (vfs_node_t *)kmalloc(sizeof(vfs_node_t));
            memset(node, 0, sizeof(vfs_node_t));
            strncpy(node->name, name, sizeof(node->name) - 1);
            node->size = e->size;
            node->flags = e->is_dir ? VFS_DIRECTORY : VFS_FILE;
            node->device_data = e;
            node->ops = &s_tarfs_ops;  /* Always assign active ops table */
            node->parent = parent;
            return node;
        }
    }

    return NULL;
}


static vfs_ops_t s_tarfs_ops = {
    .open = NULL,
    .close = NULL,
    .read = tarfs_read,
    .write = NULL,
    .lookup = tarfs_lookup,
    .readdir = tarfs_readdir
};