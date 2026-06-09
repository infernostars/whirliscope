#ifndef WHIRLISCOPE_FS_EXT2_H
#define WHIRLISCOPE_FS_EXT2_H
#pragma once

#include "userspace/abi.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*ext2_writeback_fn)(void *context, const void *image,
                                  size_t image_size);

bool ext2_mount(const void *image, size_t image_size);
bool ext2_mount_with_writeback(void *image, size_t image_size,
                               ext2_writeback_fn writeback, void *context);
bool ext2_mounted(void);
bool ext2_sync(void);
void ext2_get_status(struct user_fs_status *status);
long ext2_list_dir(const char *path, struct user_fs_dirent *entries,
                   size_t max_entries);
long ext2_stat(const char *path, struct user_fs_dirent *entry);
long ext2_read_file(const char *path, void *buffer, size_t len,
                    uint64_t offset);
long ext2_write_file(const char *path, const void *buffer, size_t len,
                     uint64_t offset);
long ext2_create_file(const char *path);
long ext2_mkdir(const char *path);
long ext2_unlink(const char *path);
long ext2_rmdir(const char *path);
long ext2_truncate_file(const char *path, uint64_t size);

#endif // WHIRLISCOPE_FS_EXT2_H
