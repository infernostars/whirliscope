#include "ext2.h"

#include <libc/mem.h>
#include <libc/string.h>

#define EXT2_SUPERBLOCK_OFFSET 1024u
#define EXT2_SUPER_MAGIC 0xef53u
#define EXT2_ROOT_INODE 2u
#define EXT2_N_BLOCKS 15u
#define EXT2_NAME_LEN 255u
#define EXT2_DIRECT_BLOCKS 12u
#define EXT2_S_IFDIR 0x4000u
#define EXT2_S_IFREG 0x8000u
#define EXT2_FILE_TYPE_REG 1u
#define EXT2_FILE_TYPE_DIR 2u
#define EXT2_NAME_OFFSET 8u

struct ext2_superblock {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t reserved_blocks_count;
    uint32_t free_blocks_count;
    uint32_t free_inodes_count;
    uint32_t first_data_block;
    uint32_t log_block_size;
    uint32_t log_frag_size;
    uint32_t blocks_per_group;
    uint32_t frags_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mnt_count;
    uint16_t max_mnt_count;
    uint16_t magic;
    uint16_t state;
    uint16_t errors;
    uint16_t minor_rev_level;
    uint32_t lastcheck;
    uint32_t checkinterval;
    uint32_t creator_os;
    uint32_t rev_level;
    uint16_t def_resuid;
    uint16_t def_resgid;
    uint32_t first_ino;
    uint16_t inode_size;
    uint16_t block_group_nr;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    uint8_t uuid[16];
    char volume_name[16];
} __attribute__((packed));

struct ext2_group_desc {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks_count;
    uint16_t free_inodes_count;
    uint16_t used_dirs_count;
    uint16_t pad;
    uint8_t reserved[12];
} __attribute__((packed));

struct ext2_inode {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[EXT2_N_BLOCKS];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t faddr;
    uint8_t osd2[12];
} __attribute__((packed));

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} __attribute__((packed));

static uint8_t *fs_image;
static size_t fs_size;
static struct ext2_superblock *superblock;
static uint32_t block_size;
static uint32_t group_count;
static ext2_writeback_fn writeback;
static void *writeback_context;

static bool image_range_valid(uint64_t offset, uint64_t len) {
    if (offset > fs_size || len > fs_size - offset) {
        return false;
    }
    return true;
}

static void *image_ptr(uint64_t offset, uint64_t len) {
    if (!image_range_valid(offset, len)) {
        return NULL;
    }
    return fs_image + offset;
}

static void *block_ptr(uint32_t block, uint64_t offset, uint64_t len) {
    uint64_t base = (uint64_t)block * block_size;
    if (offset > block_size || len > block_size - offset) {
        return NULL;
    }
    return image_ptr(base + offset, len);
}

static struct ext2_group_desc *group_desc(uint32_t group) {
    if (group >= group_count) {
        return NULL;
    }

    uint64_t table = block_size == 1024u ? 2u : 1u;
    return image_ptr(table * block_size
                   + (uint64_t)group * sizeof(struct ext2_group_desc),
                   sizeof(struct ext2_group_desc));
}

static bool read_inode(uint32_t inode_number, struct ext2_inode *out) {
    if (inode_number == 0 || inode_number > superblock->inodes_count
     || out == NULL) {
        return false;
    }

    uint32_t index = inode_number - 1;
    uint32_t group = index / superblock->inodes_per_group;
    uint32_t group_index = index % superblock->inodes_per_group;
    const struct ext2_group_desc *desc = group_desc(group);
    if (desc == NULL) {
        return false;
    }

    uint16_t inode_size = superblock->inode_size != 0
        ? superblock->inode_size
        : 128u;
    uint64_t offset = (uint64_t)desc->inode_table * block_size
        + (uint64_t)group_index * inode_size;
    const void *inode = image_ptr(offset, sizeof(*out));
    if (inode == NULL) {
        return false;
    }

    memcpy(out, inode, sizeof(*out));
    return true;
}

static bool write_inode(uint32_t inode_number, const struct ext2_inode *in) {
    if (inode_number == 0 || inode_number > superblock->inodes_count
     || in == NULL) {
        return false;
    }

    uint32_t index = inode_number - 1;
    uint32_t group = index / superblock->inodes_per_group;
    uint32_t group_index = index % superblock->inodes_per_group;
    struct ext2_group_desc *desc = group_desc(group);
    if (desc == NULL) {
        return false;
    }

    uint16_t inode_size = superblock->inode_size != 0
        ? superblock->inode_size
        : 128u;
    uint64_t offset = (uint64_t)desc->inode_table * block_size
        + (uint64_t)group_index * inode_size;
    void *inode = image_ptr(offset, sizeof(*in));
    if (inode == NULL) {
        return false;
    }

    memcpy(inode, in, sizeof(*in));
    return true;
}

static uint32_t inode_size_bytes(const struct ext2_inode *inode) {
    return inode->size;
}

static uint16_t dir_entry_min_len(size_t name_len) {
    return (uint16_t)((EXT2_NAME_OFFSET + name_len + 3u) & ~3u);
}

static bool bitmap_get(const uint8_t *bitmap, uint32_t index) {
    return (bitmap[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0;
}

static void bitmap_set(uint8_t *bitmap, uint32_t index, bool value) {
    uint8_t mask = (uint8_t)(1u << (index % 8u));
    if (value) {
        bitmap[index / 8u] |= mask;
    } else {
        bitmap[index / 8u] &= (uint8_t)~mask;
    }
}

static uint32_t inode_allocated_blocks(const struct ext2_inode *inode) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < EXT2_DIRECT_BLOCKS; i++) {
        if (inode->block[i] != 0) {
            count++;
        }
    }
    if (inode->block[12] != 0) {
        count++;
        const uint32_t *indirect = block_ptr(inode->block[12], 0, block_size);
        if (indirect != NULL) {
            uint32_t entries = block_size / sizeof(uint32_t);
            for (uint32_t i = 0; i < entries; i++) {
                if (indirect[i] != 0) {
                    count++;
                }
            }
        }
    }
    return count;
}

static void update_inode_block_count(struct ext2_inode *inode) {
    inode->blocks = inode_allocated_blocks(inode) * (block_size / 512u);
}

static bool mark_block_used(uint32_t block, bool used) {
    if (block >= superblock->blocks_count) {
        return false;
    }

    uint32_t group = (block - superblock->first_data_block)
        / superblock->blocks_per_group;
    uint32_t group_index = (block - superblock->first_data_block)
        % superblock->blocks_per_group;
    struct ext2_group_desc *desc = group_desc(group);
    if (desc == NULL) {
        return false;
    }

    uint8_t *bitmap = block_ptr(desc->block_bitmap, 0, block_size);
    if (bitmap == NULL) {
        return false;
    }

    bool old = bitmap_get(bitmap, group_index);
    if (old == used) {
        return true;
    }

    bitmap_set(bitmap, group_index, used);
    if (used) {
        if (superblock->free_blocks_count > 0) {
            superblock->free_blocks_count--;
        }
        if (desc->free_blocks_count > 0) {
            desc->free_blocks_count--;
        }
    } else {
        superblock->free_blocks_count++;
        desc->free_blocks_count++;
    }
    return true;
}

static bool mark_inode_used(uint32_t inode_number, bool used, bool is_dir) {
    if (inode_number == 0 || inode_number > superblock->inodes_count) {
        return false;
    }

    uint32_t index = inode_number - 1;
    uint32_t group = index / superblock->inodes_per_group;
    uint32_t group_index = index % superblock->inodes_per_group;
    struct ext2_group_desc *desc = group_desc(group);
    if (desc == NULL) {
        return false;
    }

    uint8_t *bitmap = block_ptr(desc->inode_bitmap, 0, block_size);
    if (bitmap == NULL) {
        return false;
    }

    bool old = bitmap_get(bitmap, group_index);
    if (old == used) {
        return true;
    }

    bitmap_set(bitmap, group_index, used);
    if (used) {
        if (superblock->free_inodes_count > 0) {
            superblock->free_inodes_count--;
        }
        if (desc->free_inodes_count > 0) {
            desc->free_inodes_count--;
        }
        if (is_dir) {
            desc->used_dirs_count++;
        }
    } else {
        superblock->free_inodes_count++;
        desc->free_inodes_count++;
        if (is_dir && desc->used_dirs_count > 0) {
            desc->used_dirs_count--;
        }
    }
    return true;
}

static uint32_t allocate_block(void) {
    for (uint32_t group = 0; group < group_count; group++) {
        struct ext2_group_desc *desc = group_desc(group);
        if (desc == NULL || desc->free_blocks_count == 0) {
            continue;
        }

        uint8_t *bitmap = block_ptr(desc->block_bitmap, 0, block_size);
        if (bitmap == NULL) {
            return 0;
        }

        uint32_t group_base = superblock->first_data_block
            + group * superblock->blocks_per_group;
        uint32_t group_limit = superblock->blocks_per_group;
        if (group_base + group_limit > superblock->blocks_count) {
            group_limit = superblock->blocks_count - group_base;
        }

        for (uint32_t i = 0; i < group_limit; i++) {
            if (!bitmap_get(bitmap, i)) {
                uint32_t block = group_base + i;
                if (!mark_block_used(block, true)) {
                    return 0;
                }
                void *ptr = block_ptr(block, 0, block_size);
                if (ptr == NULL) {
                    mark_block_used(block, false);
                    return 0;
                }
                memset(ptr, 0, block_size);
                return block;
            }
        }
    }

    return 0;
}

static uint32_t allocate_inode(bool is_dir) {
    uint32_t first = superblock->first_ino != 0 ? superblock->first_ino : 11u;
    for (uint32_t inode = first; inode <= superblock->inodes_count; inode++) {
        uint32_t index = inode - 1;
        uint32_t group = index / superblock->inodes_per_group;
        uint32_t group_index = index % superblock->inodes_per_group;
        struct ext2_group_desc *desc = group_desc(group);
        if (desc == NULL || desc->free_inodes_count == 0) {
            continue;
        }

        uint8_t *bitmap = block_ptr(desc->inode_bitmap, 0, block_size);
        if (bitmap == NULL) {
            return 0;
        }

        if (!bitmap_get(bitmap, group_index)) {
            if (!mark_inode_used(inode, true, is_dir)) {
                return 0;
            }
            struct ext2_inode empty;
            memset(&empty, 0, sizeof(empty));
            write_inode(inode, &empty);
            return inode;
        }
    }

    return 0;
}

static uint32_t block_id_at(const struct ext2_inode *inode,
                            uint32_t file_block_index) {
    if (file_block_index < EXT2_DIRECT_BLOCKS) {
        return inode->block[file_block_index];
    }

    file_block_index -= EXT2_DIRECT_BLOCKS;
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    if (file_block_index < entries_per_block && inode->block[12] != 0) {
        const uint32_t *indirect = block_ptr(inode->block[12], 0, block_size);
        return indirect != NULL ? indirect[file_block_index] : 0;
    }

    return 0;
}

static bool set_block_id_at(struct ext2_inode *inode,
                            uint32_t file_block_index,
                            uint32_t block_id,
                            bool allocate_metadata) {
    if (file_block_index < EXT2_DIRECT_BLOCKS) {
        inode->block[file_block_index] = block_id;
        return true;
    }

    file_block_index -= EXT2_DIRECT_BLOCKS;
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    if (file_block_index >= entries_per_block) {
        return false;
    }

    if (inode->block[12] == 0) {
        if (!allocate_metadata) {
            return false;
        }
        inode->block[12] = allocate_block();
        if (inode->block[12] == 0) {
            return false;
        }
    }

    uint32_t *indirect = block_ptr(inode->block[12], 0, block_size);
    if (indirect == NULL) {
        return false;
    }
    indirect[file_block_index] = block_id;
    return true;
}

static bool ensure_file_block(struct ext2_inode *inode,
                              uint32_t file_block_index,
                              uint32_t *block_out) {
    uint32_t block = block_id_at(inode, file_block_index);
    if (block != 0) {
        *block_out = block;
        return true;
    }

    block = allocate_block();
    if (block == 0) {
        return false;
    }
    if (!set_block_id_at(inode, file_block_index, block, true)) {
        mark_block_used(block, false);
        return false;
    }
    update_inode_block_count(inode);
    *block_out = block;
    return true;
}

static bool read_inode_bytes(const struct ext2_inode *inode, uint64_t offset,
                             void *buffer, size_t len) {
    uint8_t *out = buffer;
    uint64_t file_size = inode_size_bytes(inode);
    if (offset > file_size || len > file_size - offset) {
        return false;
    }

    size_t copied = 0;
    while (copied < len) {
        uint64_t file_offset = offset + copied;
        uint32_t file_block = (uint32_t)(file_offset / block_size);
        uint32_t block_offset = (uint32_t)(file_offset % block_size);
        uint32_t disk_block = block_id_at(inode, file_block);

        size_t chunk = block_size - block_offset;
        if (chunk > len - copied) {
            chunk = len - copied;
        }

        if (disk_block == 0) {
            memset(out + copied, 0, chunk);
            copied += chunk;
            continue;
        }

        const void *src = block_ptr(disk_block, block_offset, chunk);
        if (src == NULL) {
            return false;
        }
        memcpy(out + copied, src, chunk);
        copied += chunk;
    }

    return true;
}

static bool inode_is_dir(const struct ext2_inode *inode) {
    return (inode->mode & EXT2_S_IFDIR) == EXT2_S_IFDIR;
}

static bool inode_is_file(const struct ext2_inode *inode) {
    return (inode->mode & EXT2_S_IFREG) == EXT2_S_IFREG;
}

static bool names_equal(const char *entry_name, uint8_t entry_len,
                        const char *name, size_t name_len) {
    return entry_len == name_len && memcmp(entry_name, name, name_len) == 0;
}

static bool find_in_dir(const struct ext2_inode *dir, const char *name,
                        size_t name_len, uint32_t *inode_number) {
    if (!inode_is_dir(dir) || name_len == 0 || name_len > EXT2_NAME_LEN) {
        return false;
    }

    uint8_t block[4096];
    if (block_size > sizeof(block)) {
        return false;
    }

    uint32_t size = inode_size_bytes(dir);
    for (uint64_t offset = 0; offset < size; offset += block_size) {
        size_t chunk = size - offset < block_size ? size - offset : block_size;
        if (!read_inode_bytes(dir, offset, block, chunk)) {
            return false;
        }

        size_t pos = 0;
        while (pos + sizeof(struct ext2_dir_entry) <= chunk) {
            const struct ext2_dir_entry *entry =
                (const struct ext2_dir_entry *)(const void *)(block + pos);
            if (entry->rec_len < 8 || pos + entry->rec_len > chunk) {
                return false;
            }
            if (entry->inode != 0
             && names_equal(entry->name, entry->name_len, name, name_len)) {
                *inode_number = entry->inode;
                return true;
            }
            pos += entry->rec_len;
        }
    }

    return false;
}

static bool dir_is_empty(const struct ext2_inode *dir) {
    if (!inode_is_dir(dir)) {
        return false;
    }

    uint8_t block[4096];
    if (block_size > sizeof(block)) {
        return false;
    }

    uint32_t size = inode_size_bytes(dir);
    for (uint64_t offset = 0; offset < size; offset += block_size) {
        size_t chunk = size - offset < block_size ? size - offset : block_size;
        if (!read_inode_bytes(dir, offset, block, chunk)) {
            return false;
        }

        size_t pos = 0;
        while (pos + sizeof(struct ext2_dir_entry) <= chunk) {
            const struct ext2_dir_entry *entry =
                (const struct ext2_dir_entry *)(const void *)(block + pos);
            if (entry->rec_len < 8 || pos + entry->rec_len > chunk) {
                return false;
            }
            if (entry->inode != 0
             && !(entry->name_len == 1 && entry->name[0] == '.')
             && !(entry->name_len == 2 && entry->name[0] == '.'
                                      && entry->name[1] == '.')) {
                return false;
            }
            pos += entry->rec_len;
        }
    }

    return true;
}

static bool path_lookup(const char *path, struct ext2_inode *inode_out,
                        uint32_t *inode_number_out) {
    if (!ext2_mounted() || path == NULL || path[0] != '/') {
        return false;
    }

    struct ext2_inode current;
    uint32_t current_inode = EXT2_ROOT_INODE;
    if (!read_inode(current_inode, &current)) {
        return false;
    }

    const char *cursor = path;
    while (*cursor == '/') {
        cursor++;
    }

    while (*cursor != '\0') {
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        size_t len = (size_t)(cursor - start);

        if (len != 0) {
            uint32_t next_inode;
            if (!find_in_dir(&current, start, len, &next_inode)
             || !read_inode(next_inode, &current)) {
                return false;
            }
            current_inode = next_inode;
        }

        while (*cursor == '/') {
            cursor++;
        }
    }

    *inode_out = current;
    if (inode_number_out != NULL) {
        *inode_number_out = current_inode;
    }
    return true;
}

static bool split_parent_path(const char *path, char *parent,
                              size_t parent_size, const char **name,
                              size_t *name_len) {
    if (path == NULL || parent == NULL || parent_size == 0 || name == NULL
     || name_len == NULL || path[0] != '/') {
        return false;
    }

    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }
    if (len <= 1) {
        return false;
    }

    size_t slash = len - 1;
    while (slash > 0 && path[slash] != '/') {
        slash--;
    }

    *name = path + slash + 1;
    *name_len = len - slash - 1;
    if (*name_len == 0 || *name_len > EXT2_NAME_LEN) {
        return false;
    }

    if (slash == 0) {
        if (parent_size < 2) {
            return false;
        }
        parent[0] = '/';
        parent[1] = '\0';
        return true;
    }

    if (slash + 1 > parent_size) {
        return false;
    }
    memcpy(parent, path, slash);
    parent[slash] = '\0';
    return true;
}

static bool parent_lookup(const char *path, struct ext2_inode *parent,
                          uint32_t *parent_inode_number,
                          const char **name, size_t *name_len) {
    char parent_path[USER_FS_PATH_SIZE];
    if (!split_parent_path(path, parent_path, sizeof(parent_path), name,
                           name_len)) {
        return false;
    }

    return path_lookup(parent_path, parent, parent_inode_number)
        && inode_is_dir(parent);
}

static bool add_dir_entry(struct ext2_inode *dir, uint32_t dir_inode_number,
                          uint32_t child_inode_number, const char *name,
                          size_t name_len, uint8_t file_type) {
    if (!inode_is_dir(dir) || name_len == 0 || name_len > EXT2_NAME_LEN) {
        return false;
    }

    uint16_t needed = dir_entry_min_len(name_len);
    uint32_t size = inode_size_bytes(dir);
    uint32_t blocks = (size + block_size - 1u) / block_size;
    if (blocks == 0) {
        blocks = 1;
    }

    for (uint32_t file_block = 0; file_block < blocks; file_block++) {
        uint32_t disk_block;
        if (!ensure_file_block(dir, file_block, &disk_block)) {
            return false;
        }

        uint8_t *block = block_ptr(disk_block, 0, block_size);
        if (block == NULL) {
            return false;
        }

        size_t pos = 0;
        while (pos + sizeof(struct ext2_dir_entry) <= block_size) {
            struct ext2_dir_entry *entry =
                (struct ext2_dir_entry *)(void *)(block + pos);
            if (entry->rec_len < 8 || pos + entry->rec_len > block_size) {
                return false;
            }

            uint16_t actual = entry->inode != 0
                ? dir_entry_min_len(entry->name_len)
                : 8u;
            if (entry->rec_len >= actual + needed) {
                uint16_t old_rec_len = entry->rec_len;
                if (entry->inode != 0) {
                    entry->rec_len = actual;
                    pos += actual;
                    entry = (struct ext2_dir_entry *)(void *)(block + pos);
                    entry->rec_len = old_rec_len - actual;
                }

                entry->inode = child_inode_number;
                entry->name_len = (uint8_t)name_len;
                entry->file_type = file_type;
                memcpy(entry->name, name, name_len);
                dir->size = blocks * block_size;
                update_inode_block_count(dir);
                return write_inode(dir_inode_number, dir);
            }

            pos += entry->rec_len;
        }
    }

    uint32_t file_block = blocks;
    uint32_t disk_block;
    if (!ensure_file_block(dir, file_block, &disk_block)) {
        return false;
    }

    uint8_t *block = block_ptr(disk_block, 0, block_size);
    if (block == NULL) {
        return false;
    }
    memset(block, 0, block_size);
    struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(void *)block;
    entry->inode = child_inode_number;
    entry->rec_len = block_size;
    entry->name_len = (uint8_t)name_len;
    entry->file_type = file_type;
    memcpy(entry->name, name, name_len);
    dir->size = (blocks + 1u) * block_size;
    update_inode_block_count(dir);
    return write_inode(dir_inode_number, dir);
}

static bool remove_dir_entry(struct ext2_inode *dir, uint32_t dir_inode_number,
                             const char *name, size_t name_len,
                             uint32_t *removed_inode) {
    if (!inode_is_dir(dir) || name_len == 0 || name_len > EXT2_NAME_LEN) {
        return false;
    }

    uint32_t size = inode_size_bytes(dir);
    for (uint64_t offset = 0; offset < size; offset += block_size) {
        uint32_t file_block = (uint32_t)(offset / block_size);
        uint32_t disk_block = block_id_at(dir, file_block);
        if (disk_block == 0) {
            continue;
        }

        uint8_t *block = block_ptr(disk_block, 0, block_size);
        if (block == NULL) {
            return false;
        }

        size_t pos = 0;
        struct ext2_dir_entry *previous = NULL;
        while (pos + sizeof(struct ext2_dir_entry) <= block_size) {
            struct ext2_dir_entry *entry =
                (struct ext2_dir_entry *)(void *)(block + pos);
            if (entry->rec_len < 8 || pos + entry->rec_len > block_size) {
                return false;
            }

            if (entry->inode != 0
             && names_equal(entry->name, entry->name_len, name, name_len)) {
                if (removed_inode != NULL) {
                    *removed_inode = entry->inode;
                }
                if (previous != NULL) {
                    previous->rec_len += entry->rec_len;
                } else {
                    entry->inode = 0;
                }
                return write_inode(dir_inode_number, dir);
            }

            previous = entry->inode != 0 ? entry : previous;
            pos += entry->rec_len;
        }
    }

    return false;
}

static bool free_inode_blocks(struct ext2_inode *inode, uint32_t keep_blocks) {
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    uint32_t max_blocks = EXT2_DIRECT_BLOCKS + entries_per_block;
    if (keep_blocks > max_blocks) {
        return false;
    }

    for (uint32_t i = keep_blocks; i < EXT2_DIRECT_BLOCKS; i++) {
        if (inode->block[i] != 0) {
            mark_block_used(inode->block[i], false);
            inode->block[i] = 0;
        }
    }

    if (inode->block[12] != 0) {
        uint32_t *indirect = block_ptr(inode->block[12], 0, block_size);
        if (indirect == NULL) {
            return false;
        }

        uint32_t indirect_keep = keep_blocks > EXT2_DIRECT_BLOCKS
            ? keep_blocks - EXT2_DIRECT_BLOCKS
            : 0;
        for (uint32_t i = indirect_keep; i < entries_per_block; i++) {
            if (indirect[i] != 0) {
                mark_block_used(indirect[i], false);
                indirect[i] = 0;
            }
        }

        if (keep_blocks <= EXT2_DIRECT_BLOCKS) {
            mark_block_used(inode->block[12], false);
            inode->block[12] = 0;
        }
    }

    update_inode_block_count(inode);
    return true;
}

static bool resize_inode_data(struct ext2_inode *inode, uint32_t inode_number,
                              uint64_t new_size) {
    uint32_t entries_per_block = block_size / sizeof(uint32_t);
    uint64_t max_size =
        (uint64_t)(EXT2_DIRECT_BLOCKS + entries_per_block) * block_size;
    if (new_size > max_size) {
        return false;
    }

    uint64_t old_size = inode_size_bytes(inode);
    uint32_t old_blocks = (old_size + block_size - 1u) / block_size;
    uint32_t new_blocks = (new_size + block_size - 1u) / block_size;

    if (new_blocks > old_blocks) {
        for (uint32_t i = old_blocks; i < new_blocks; i++) {
            uint32_t block;
            if (!ensure_file_block(inode, i, &block)) {
                return false;
            }
        }
    } else if (new_blocks < old_blocks) {
        if (!free_inode_blocks(inode, new_blocks)) {
            return false;
        }
    }

    inode->size = (uint32_t)new_size;
    if (new_size > old_size && old_size % block_size != 0) {
        uint32_t file_block = (uint32_t)(old_size / block_size);
        uint32_t block_offset = (uint32_t)(old_size % block_size);
        uint32_t disk_block = block_id_at(inode, file_block);
        uint32_t clear_len = block_size - block_offset;
        uint8_t *tail = block_ptr(disk_block, block_offset, clear_len);
        if (tail != NULL) {
            memset(tail, 0, clear_len);
        }
    }
    update_inode_block_count(inode);
    return write_inode(inode_number, inode);
}

static uint8_t user_file_type(const struct ext2_inode *inode,
                              uint8_t dirent_type) {
    if (dirent_type == USER_FS_TYPE_DIR || inode_is_dir(inode)) {
        return USER_FS_TYPE_DIR;
    }
    if (dirent_type == USER_FS_TYPE_FILE || inode_is_file(inode)) {
        return USER_FS_TYPE_FILE;
    }
    return USER_FS_TYPE_UNKNOWN;
}

static bool ext2_mount_internal(void *image, size_t image_size,
                                ext2_writeback_fn writeback_fn,
                                void *context) {
    fs_image = NULL;
    fs_size = 0;
    superblock = NULL;
    block_size = 0;
    group_count = 0;
    writeback = NULL;
    writeback_context = NULL;

    if (image == NULL || image_size < EXT2_SUPERBLOCK_OFFSET
        + sizeof(struct ext2_superblock)) {
        return false;
    }

    fs_image = (uint8_t *)(uintptr_t)image;
    fs_size = image_size;
    superblock = image_ptr(EXT2_SUPERBLOCK_OFFSET, sizeof(*superblock));
    if (superblock == NULL || superblock->magic != EXT2_SUPER_MAGIC
     || superblock->log_block_size > 2
     || superblock->blocks_per_group == 0
     || superblock->inodes_per_group == 0) {
        fs_image = NULL;
        superblock = NULL;
        return false;
    }

    block_size = 1024u << superblock->log_block_size;
    group_count = (superblock->blocks_count + superblock->blocks_per_group - 1)
        / superblock->blocks_per_group;
    if (group_desc(0) == NULL) {
        return false;
    }

    writeback = writeback_fn;
    writeback_context = context;
    return true;
}

bool ext2_mount(const void *image, size_t image_size) {
    return ext2_mount_internal((void *)(uintptr_t)image, image_size, NULL,
                               NULL);
}

bool ext2_mount_with_writeback(void *image, size_t image_size,
                               ext2_writeback_fn writeback_fn,
                               void *context) {
    return ext2_mount_internal(image, image_size, writeback_fn, context);
}

bool ext2_mounted(void) {
    return fs_image != NULL && superblock != NULL;
}

bool ext2_sync(void) {
    if (!ext2_mounted()) {
        return false;
    }
    if (writeback == NULL) {
        return true;
    }
    return writeback(writeback_context, fs_image, fs_size);
}

void ext2_get_status(struct user_fs_status *status) {
    memset(status, 0, sizeof(*status));
    status->abi_version = USER_FS_STATUS_ABI_VERSION;
    if (!ext2_mounted()) {
        return;
    }

    status->mounted = 1;
    status->block_size = block_size;
    status->blocks = superblock->blocks_count;
    status->inodes = superblock->inodes_count;
    status->free_blocks = superblock->free_blocks_count;
    status->free_inodes = superblock->free_inodes_count;
    memcpy(status->volume_name, superblock->volume_name,
           sizeof(status->volume_name));
    status->volume_name[sizeof(status->volume_name) - 1] = '\0';
}

long ext2_list_dir(const char *path, struct user_fs_dirent *entries,
                   size_t max_entries) {
    struct ext2_inode dir;
    if (!path_lookup(path, &dir, NULL) || !inode_is_dir(&dir)) {
        return USER_ERR_INVAL;
    }

    uint8_t block[4096];
    if (block_size > sizeof(block)) {
        return USER_ERR_INVAL;
    }

    size_t written = 0;
    uint32_t size = inode_size_bytes(&dir);
    for (uint64_t offset = 0; offset < size; offset += block_size) {
        size_t chunk = size - offset < block_size ? size - offset : block_size;
        if (!read_inode_bytes(&dir, offset, block, chunk)) {
            return USER_ERR_FAULT;
        }

        size_t pos = 0;
        while (pos + sizeof(struct ext2_dir_entry) <= chunk) {
            const struct ext2_dir_entry *entry =
                (const struct ext2_dir_entry *)(const void *)(block + pos);
            if (entry->rec_len < 8 || pos + entry->rec_len > chunk) {
                return USER_ERR_FAULT;
            }
            if (entry->inode != 0
             && !(entry->name_len == 1 && entry->name[0] == '.')
             && !(entry->name_len == 2 && entry->name[0] == '.'
                                      && entry->name[1] == '.')) {
                if (written >= max_entries) {
                    return (long)written;
                }

                struct ext2_inode child;
                memset(&child, 0, sizeof(child));
                read_inode(entry->inode, &child);

                struct user_fs_dirent *out = &entries[written++];
                memset(out, 0, sizeof(*out));
                out->inode = entry->inode;
                out->type = user_file_type(&child, entry->file_type);
                out->size = inode_size_bytes(&child);
                size_t name_len = entry->name_len;
                if (name_len >= sizeof(out->name)) {
                    name_len = sizeof(out->name) - 1;
                }
                memcpy(out->name, entry->name, name_len);
                out->name[name_len] = '\0';
            }
            pos += entry->rec_len;
        }
    }

    return (long)written;
}

long ext2_read_file(const char *path, void *buffer, size_t len,
                    uint64_t offset) {
    struct ext2_inode file;
    if (!path_lookup(path, &file, NULL) || !inode_is_file(&file)) {
        return USER_ERR_INVAL;
    }

    uint64_t size = inode_size_bytes(&file);
    if (offset >= size) {
        return 0;
    }
    if (len > size - offset) {
        len = (size_t)(size - offset);
    }

    if (!read_inode_bytes(&file, offset, buffer, len)) {
        return USER_ERR_FAULT;
    }
    return (long)len;
}

long ext2_stat(const char *path, struct user_fs_dirent *entry) {
    struct ext2_inode inode;
    uint32_t inode_number;
    if (entry == NULL || !path_lookup(path, &inode, &inode_number)) {
        return USER_ERR_INVAL;
    }

    memset(entry, 0, sizeof(*entry));
    entry->inode = inode_number;
    entry->type = inode_is_dir(&inode) ? USER_FS_TYPE_DIR
        : inode_is_file(&inode) ? USER_FS_TYPE_FILE
        : USER_FS_TYPE_UNKNOWN;
    entry->size = inode_size_bytes(&inode);
    return 0;
}

long ext2_write_file(const char *path, const void *buffer, size_t len,
                     uint64_t offset) {
    const uint8_t *in = buffer;
    struct ext2_inode file;
    uint32_t inode_number;
    if ((buffer == NULL && len != 0) || !path_lookup(path, &file, &inode_number)
     || !inode_is_file(&file)) {
        return USER_ERR_INVAL;
    }

    uint64_t size = inode_size_bytes(&file);
    if (offset > size || offset + len < offset) {
        return USER_ERR_INVAL;
    }

    uint64_t end = offset + len;
    if (end > size && !resize_inode_data(&file, inode_number, end)) {
        return USER_ERR_FAULT;
    }

    size_t written = 0;
    while (written < len) {
        uint64_t file_offset = offset + written;
        uint32_t file_block = (uint32_t)(file_offset / block_size);
        uint32_t block_offset = (uint32_t)(file_offset % block_size);
        uint32_t disk_block;
        if (!ensure_file_block(&file, file_block, &disk_block)) {
            return USER_ERR_FAULT;
        }

        size_t chunk = block_size - block_offset;
        if (chunk > len - written) {
            chunk = len - written;
        }

        void *dest = block_ptr(disk_block, block_offset, chunk);
        if (dest == NULL) {
            return USER_ERR_FAULT;
        }
        memcpy(dest, in + written, chunk);
        written += chunk;
    }

    if (!write_inode(inode_number, &file) || !ext2_sync()) {
        return USER_ERR_FAULT;
    }
    return (long)written;
}

long ext2_create_file(const char *path) {
    struct ext2_inode existing;
    if (path_lookup(path, &existing, NULL)) {
        return USER_ERR_INVAL;
    }

    struct ext2_inode parent;
    uint32_t parent_inode_number;
    const char *name;
    size_t name_len;
    if (!parent_lookup(path, &parent, &parent_inode_number, &name, &name_len)) {
        return USER_ERR_INVAL;
    }

    if (find_in_dir(&parent, name, name_len, &parent_inode_number)) {
        return USER_ERR_INVAL;
    }

    uint32_t inode_number = allocate_inode(false);
    if (inode_number == 0) {
        return USER_ERR_FAULT;
    }

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFREG | 0644u;
    inode.links_count = 1;
    if (!write_inode(inode_number, &inode)
     || !add_dir_entry(&parent, parent_inode_number, inode_number, name,
                       name_len, EXT2_FILE_TYPE_REG)) {
        mark_inode_used(inode_number, false, false);
        return USER_ERR_FAULT;
    }

    return ext2_sync() ? 0 : USER_ERR_FAULT;
}

long ext2_mkdir(const char *path) {
    struct ext2_inode existing;
    if (path_lookup(path, &existing, NULL)) {
        return USER_ERR_INVAL;
    }

    struct ext2_inode parent;
    uint32_t parent_inode_number;
    const char *name;
    size_t name_len;
    if (!parent_lookup(path, &parent, &parent_inode_number, &name, &name_len)) {
        return USER_ERR_INVAL;
    }

    uint32_t inode_number = allocate_inode(true);
    uint32_t block = allocate_block();
    if (inode_number == 0 || block == 0) {
        if (inode_number != 0) {
            mark_inode_used(inode_number, false, true);
        }
        if (block != 0) {
            mark_block_used(block, false);
        }
        return USER_ERR_FAULT;
    }

    struct ext2_inode inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode = EXT2_S_IFDIR | 0755u;
    inode.size = block_size;
    inode.links_count = 2;
    inode.block[0] = block;
    update_inode_block_count(&inode);

    uint8_t *data = block_ptr(block, 0, block_size);
    if (data == NULL) {
        mark_block_used(block, false);
        mark_inode_used(inode_number, false, true);
        return USER_ERR_FAULT;
    }
    memset(data, 0, block_size);
    struct ext2_dir_entry *dot = (struct ext2_dir_entry *)(void *)data;
    dot->inode = inode_number;
    dot->rec_len = dir_entry_min_len(1);
    dot->name_len = 1;
    dot->file_type = EXT2_FILE_TYPE_DIR;
    dot->name[0] = '.';

    struct ext2_dir_entry *dotdot =
        (struct ext2_dir_entry *)(void *)(data + dot->rec_len);
    dotdot->inode = parent_inode_number;
    dotdot->rec_len = block_size - dot->rec_len;
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FILE_TYPE_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';

    parent.links_count++;
    if (!write_inode(inode_number, &inode)
     || !write_inode(parent_inode_number, &parent)
     || !add_dir_entry(&parent, parent_inode_number, inode_number, name,
                       name_len, EXT2_FILE_TYPE_DIR)) {
        mark_block_used(block, false);
        mark_inode_used(inode_number, false, true);
        return USER_ERR_FAULT;
    }

    return ext2_sync() ? 0 : USER_ERR_FAULT;
}

long ext2_unlink(const char *path) {
    struct ext2_inode parent;
    uint32_t parent_inode_number;
    const char *name;
    size_t name_len;
    if (!parent_lookup(path, &parent, &parent_inode_number, &name, &name_len)) {
        return USER_ERR_INVAL;
    }

    uint32_t inode_number;
    if (!find_in_dir(&parent, name, name_len, &inode_number)) {
        return USER_ERR_INVAL;
    }

    struct ext2_inode inode;
    if (!read_inode(inode_number, &inode) || !inode_is_file(&inode)) {
        return USER_ERR_INVAL;
    }

    uint32_t removed;
    if (!remove_dir_entry(&parent, parent_inode_number, name, name_len,
                          &removed) || removed != inode_number) {
        return USER_ERR_FAULT;
    }

    free_inode_blocks(&inode, 0);
    memset(&inode, 0, sizeof(inode));
    write_inode(inode_number, &inode);
    mark_inode_used(inode_number, false, false);
    return ext2_sync() ? 0 : USER_ERR_FAULT;
}

long ext2_rmdir(const char *path) {
    struct ext2_inode parent;
    uint32_t parent_inode_number;
    const char *name;
    size_t name_len;
    if (!parent_lookup(path, &parent, &parent_inode_number, &name, &name_len)
     || (name_len == 1 && name[0] == '.')
     || (name_len == 2 && name[0] == '.' && name[1] == '.')) {
        return USER_ERR_INVAL;
    }

    uint32_t inode_number;
    if (!find_in_dir(&parent, name, name_len, &inode_number)) {
        return USER_ERR_INVAL;
    }

    struct ext2_inode inode;
    if (!read_inode(inode_number, &inode) || !inode_is_dir(&inode)
     || !dir_is_empty(&inode)) {
        return USER_ERR_INVAL;
    }

    uint32_t removed;
    if (!remove_dir_entry(&parent, parent_inode_number, name, name_len,
                          &removed) || removed != inode_number) {
        return USER_ERR_FAULT;
    }

    if (parent.links_count > 0) {
        parent.links_count--;
        write_inode(parent_inode_number, &parent);
    }
    free_inode_blocks(&inode, 0);
    memset(&inode, 0, sizeof(inode));
    write_inode(inode_number, &inode);
    mark_inode_used(inode_number, false, true);
    return ext2_sync() ? 0 : USER_ERR_FAULT;
}

long ext2_truncate_file(const char *path, uint64_t size) {
    struct ext2_inode file;
    uint32_t inode_number;
    if (!path_lookup(path, &file, &inode_number) || !inode_is_file(&file)) {
        return USER_ERR_INVAL;
    }

    if (!resize_inode_data(&file, inode_number, size)) {
        return USER_ERR_FAULT;
    }
    return ext2_sync() ? 0 : USER_ERR_FAULT;
}
