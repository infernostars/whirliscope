#include "rootfs.h"

#include "drivers/block.h"
#include "fs/ext2.h"
#include "kernel/klog.h"
#include "memory/heap.h"
#include <libc/mem.h>
#include <libc/string.h>
#include <stddef.h>
#include <stdint.h>

#define ROOTFS_LABEL "WHIRLROOT"
#define ROOTFS_MAX_BYTES (64ull * 1024ull * 1024ull)
#define EXT2_SUPERBLOCK_OFFSET 1024u
#define EXT2_SUPER_MAGIC 0xef53u

struct rootfs_ext2_superblock {
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

struct rootfs_disk {
    struct block_device *device;
    uint64_t sector_count;
};

static struct rootfs_disk mounted_rootfs;

static bool rootfs_writeback(void *context, const void *image,
                             size_t image_size) {
    struct rootfs_disk *rootfs = context;
    if (rootfs == NULL || rootfs->device == NULL || image == NULL
     || image_size == 0) {
        return false;
    }

    uint64_t bytes = rootfs->sector_count * rootfs->device->sector_size;
    if (image_size > bytes) {
        return false;
    }

    bool ok = block_write(rootfs->device, 0, image,
                          (size_t)rootfs->sector_count);
    if (!ok) {
        klog_printf("rootfs: writeback failed on %s\n",
                    rootfs->device->name);
    }
    return ok;
}

static bool validate_candidate(struct block_device *device,
                               uint64_t *fs_bytes,
                               uint64_t *fs_sectors) {
    uint8_t probe[2048];
    if (device->sector_size != 512 || device->sector_count < 4) {
        klog_printf("rootfs: skipping %s unsupported sector geometry\n",
                    device->name);
        return false;
    }

    if (!block_read(device, 0, probe, sizeof(probe) / 512u)) {
        klog_printf("rootfs: skipping %s initial read failed\n",
                    device->name);
        return false;
    }

    const struct rootfs_ext2_superblock *sb =
        (const struct rootfs_ext2_superblock *)(const void *)
        (probe + EXT2_SUPERBLOCK_OFFSET);
    if (sb->magic != EXT2_SUPER_MAGIC) {
        klog_printf("rootfs: skipping %s no ext2 magic\n", device->name);
        return false;
    }
    if (sb->log_block_size > 2 || sb->blocks_count == 0) {
        klog_printf("rootfs: skipping %s unsupported ext2 block size\n",
                    device->name);
        return false;
    }
    if (memcmp(sb->volume_name, ROOTFS_LABEL, strlen(ROOTFS_LABEL)) != 0) {
        klog_printf("rootfs: skipping %s ext2 label is not %s\n",
                    device->name, ROOTFS_LABEL);
        return false;
    }

    uint64_t block_size = 1024ull << sb->log_block_size;
    uint64_t bytes = (uint64_t)sb->blocks_count * block_size;
    if (bytes == 0 || bytes > ROOTFS_MAX_BYTES) {
        klog_printf("rootfs: skipping %s ext2 image too large (%llu bytes)\n",
                    device->name, (unsigned long long)bytes);
        return false;
    }

    uint64_t sectors = (bytes + device->sector_size - 1)
        / device->sector_size;
    if (sectors > device->sector_count) {
        klog_printf("rootfs: skipping %s ext2 exceeds device size\n",
                    device->name);
        return false;
    }

    *fs_bytes = sectors * device->sector_size;
    *fs_sectors = sectors;
    return true;
}

static bool try_mount_device(struct block_device *device) {
    uint64_t fs_bytes;
    uint64_t fs_sectors;
    if (!validate_candidate(device, &fs_bytes, &fs_sectors)) {
        return false;
    }

    void *image = kmalloc((size_t)fs_bytes);
    if (image == NULL) {
        klog_printf("rootfs: cannot allocate %llu bytes for %s\n",
                    (unsigned long long)fs_bytes, device->name);
        return false;
    }

    if (!block_read(device, 0, image, (size_t)fs_sectors)) {
        klog_printf("rootfs: full read failed from %s\n", device->name);
        kfree(image);
        return false;
    }

    mounted_rootfs.device = device;
    mounted_rootfs.sector_count = fs_sectors;
    if (!ext2_mount_with_writeback(image, (size_t)fs_bytes, rootfs_writeback,
                                   &mounted_rootfs)) {
        klog_printf("rootfs: ext2 mount failed from %s\n", device->name);
        memset(&mounted_rootfs, 0, sizeof(mounted_rootfs));
        kfree(image);
        return false;
    }

    klog_printf("rootfs: mounted writable ext2 from %s bytes=%llu\n",
                device->name, (unsigned long long)fs_bytes);
    return true;
}

bool rootfs_mount_from_block_devices(void) {
    for (size_t i = 0; i < block_device_count(); i++) {
        struct block_device *device = block_device_at(i);
        if (device != NULL && device->writable && try_mount_device(device)) {
            return true;
        }
    }

    klog_printf("rootfs: no writable block rootfs found\n");
    return false;
}
