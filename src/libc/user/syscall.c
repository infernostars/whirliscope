#include "syscall.h"

long user_syscall0(long number) {
    register long rax asm("rax") = number;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  :
                  : "rcx", "rdx", "rdi", "rsi", "r8", "r9", "r10", "r11",
                    "memory");
    return rax;
}

long user_syscall1(long number, long arg0) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi)
                  : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
                    "memory");
    return rax;
}

long user_syscall2(long number, long arg0, long arg1) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;
    register long rsi asm("rsi") = arg1;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi)
                  : "rcx", "rdx", "r8", "r9", "r10", "r11", "memory");
    return rax;
}

long user_syscall3(long number, long arg0, long arg1, long arg2) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;
    register long rsi asm("rsi") = arg1;
    register long rdx asm("rdx") = arg2;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi), "d"(rdx)
                  : "rcx", "r8", "r9", "r10", "r11", "memory");
    return rax;
}

long user_syscall4(long number, long arg0, long arg1, long arg2, long arg3) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;
    register long rsi asm("rsi") = arg1;
    register long rdx asm("rdx") = arg2;
    register long r10 asm("r10") = arg3;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi), "d"(rdx), "r"(r10)
                  : "rcx", "r8", "r9", "r11", "memory");
    return rax;
}

void user_exit(long status) {
    user_syscall1(SYSCALL_EXIT, status);
    for (;;) {
        asm volatile ("hlt");
    }
}

long user_write(const char *s, size_t len) {
    return user_syscall2(SYSCALL_DEBUG_WRITE, (long)s, (long)len);
}

long user_read_event(struct user_input_event *event) {
    long packed = user_syscall0(SYSCALL_READ_EVENT);
    if (packed < 0) {
        return packed;
    }

    event->key = (enum user_keyboard_key)(packed & 0xff);
    event->ch = (char)((packed >> 8) & 0xff);
    event->modifiers = (uint8_t)((packed >> 16) & 0xff);
    return 0;
}

long user_clear(void) {
    return user_syscall0(SYSCALL_CLEAR);
}

long user_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    return user_syscall3(SYSCALL_SET_COLOR, red, green, blue);
}

long user_set_background(uint8_t red, uint8_t green, uint8_t blue) {
    return user_syscall3(SYSCALL_SET_BACKGROUND, red, green, blue);
}

long user_reset_colors(void) {
    return user_syscall0(SYSCALL_RESET_COLORS);
}

long user_memory_status(struct user_memory_status *status) {
    return user_syscall1(SYSCALL_MEMORY_STATUS, (long)status);
}

long user_userspace_status(struct user_userspace_status *status) {
    return user_syscall1(SYSCALL_USERSPACE_STATUS, (long)status);
}

long user_klog_status(struct user_klog_status *status) {
    return user_syscall1(SYSCALL_KLOG_STATUS, (long)status);
}

long user_klog_read(char *buffer, size_t len) {
    return user_syscall2(SYSCALL_KLOG_READ, (long)buffer, (long)len);
}

long user_timer_status(struct user_timer_status *status) {
    return user_syscall1(SYSCALL_TIMER_STATUS, (long)status);
}

long user_framebuffer_status(struct user_framebuffer_status *status) {
    return user_syscall1(SYSCALL_FRAMEBUFFER_STATUS, (long)status);
}

long user_kernel_heap_status(struct user_kernel_heap_status *status) {
    return user_syscall1(SYSCALL_KERNEL_HEAP_STATUS, (long)status);
}

long user_app_count(void) {
    return user_syscall0(SYSCALL_APP_COUNT);
}

long user_app_info(uint32_t index, struct user_app_info *info) {
    return user_syscall2(SYSCALL_APP_INFO, index, (long)info);
}

long user_app_run(uint32_t index) {
    return user_syscall1(SYSCALL_APP_RUN, index);
}

long user_app_run_path(const char *path) {
    return user_syscall1(SYSCALL_APP_RUN_PATH, (long)path);
}

long user_fs_status(struct user_fs_status *status) {
    return user_syscall1(SYSCALL_FS_STATUS, (long)status);
}

long user_fs_list(const char *path, struct user_fs_dirent *entries,
                  size_t max_entries) {
    return user_syscall3(SYSCALL_FS_LIST, (long)path, (long)entries,
                         (long)max_entries);
}

long user_fs_read(const char *path, void *buffer, size_t len, uint64_t offset) {
    return user_syscall4(SYSCALL_FS_READ, (long)path, (long)buffer,
                         (long)len, (long)offset);
}

long user_fs_getcwd(char *buffer, size_t len) {
    return user_syscall2(SYSCALL_FS_GETCWD, (long)buffer, (long)len);
}

long user_fs_chdir(const char *path) {
    return user_syscall1(SYSCALL_FS_CHDIR, (long)path);
}

long user_fs_write(const char *path, const void *buffer, size_t len,
                   uint64_t offset) {
    return user_syscall4(SYSCALL_FS_WRITE, (long)path, (long)buffer,
                         (long)len, (long)offset);
}

long user_fs_create(const char *path) {
    return user_syscall1(SYSCALL_FS_CREATE, (long)path);
}

long user_fs_mkdir(const char *path) {
    return user_syscall1(SYSCALL_FS_MKDIR, (long)path);
}

long user_fs_unlink(const char *path) {
    return user_syscall1(SYSCALL_FS_UNLINK, (long)path);
}

long user_fs_rmdir(const char *path) {
    return user_syscall1(SYSCALL_FS_RMDIR, (long)path);
}

long user_fs_truncate(const char *path, uint64_t size) {
    return user_syscall2(SYSCALL_FS_TRUNCATE, (long)path, (long)size);
}

void *user_sbrk(int64_t increment) {
    long result = user_syscall1(SYSCALL_SBRK, increment);
    return result < 0 ? (void *)0 : (void *)result;
}
