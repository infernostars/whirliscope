#ifndef WHIRLISCOPE_USER_LIBC_SYSCALL_H
#define WHIRLISCOPE_USER_LIBC_SYSCALL_H
#pragma once

#include "userspace/abi.h"
#include <stddef.h>
#include <stdint.h>

long user_syscall0(long number);
long user_syscall1(long number, long arg0);
long user_syscall2(long number, long arg0, long arg1);
long user_syscall3(long number, long arg0, long arg1, long arg2);
long user_syscall4(long number, long arg0, long arg1, long arg2, long arg3);

void user_exit(long status);
long user_write(const char *s, size_t len);
long user_read_event(struct user_input_event *event);
long user_clear(void);
long user_set_color(uint8_t red, uint8_t green, uint8_t blue);
long user_set_background(uint8_t red, uint8_t green, uint8_t blue);
long user_reset_colors(void);
long user_memory_status(struct user_memory_status *status);
long user_userspace_status(struct user_userspace_status *status);
long user_klog_status(struct user_klog_status *status);
long user_klog_read(char *buffer, size_t len);
long user_timer_status(struct user_timer_status *status);
long user_framebuffer_status(struct user_framebuffer_status *status);
long user_kernel_heap_status(struct user_kernel_heap_status *status);
long user_app_count(void);
long user_app_info(uint32_t index, struct user_app_info *info);
long user_app_run(uint32_t index);
long user_app_run_path(const char *path);
long user_fs_status(struct user_fs_status *status);
long user_fs_list(const char *path, struct user_fs_dirent *entries,
                  size_t max_entries);
long user_fs_read(const char *path, void *buffer, size_t len, uint64_t offset);
long user_fs_getcwd(char *buffer, size_t len);
long user_fs_chdir(const char *path);
long user_fs_write(const char *path, const void *buffer, size_t len,
                   uint64_t offset);
long user_fs_create(const char *path);
long user_fs_mkdir(const char *path);
long user_fs_unlink(const char *path);
long user_fs_rmdir(const char *path);
long user_fs_truncate(const char *path, uint64_t size);
void *user_sbrk(int64_t increment);

#endif // WHIRLISCOPE_USER_LIBC_SYSCALL_H
