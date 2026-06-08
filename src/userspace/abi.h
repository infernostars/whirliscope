#ifndef WHIRLISCOPE_USERSPACE_ABI_H
#define WHIRLISCOPE_USERSPACE_ABI_H
#pragma once

#include <stdint.h>

#define USERSPACE_ABI_VERSION 1u
#define USERSPACE_STATUS_ABI_VERSION 1u
#define USERSPACE_APP_INFO_ABI_VERSION 1u
#define USERSPACE_APP_NAME_SIZE 32u

#define USER_ERR_AGAIN (-1L)
#define USER_ERR_FAULT (-2L)
#define USER_ERR_INVAL (-3L)
#define USER_ERR_NOSYS (-4L)

/*
 * Syscall ABI policy:
 * - syscall number goes in rax; arg0..arg5 go in rdi, rsi, rdx, r10, r8, r9.
 * - non-negative return values are syscall-specific success values.
 * - negative return values are stable USER_ERR_* values.
 * - userspace pointers are never trusted; kernels must range-check and copy.
 * - pointer-output structs include an abi_version when they are expected to grow.
 * - new syscalls are appended; existing syscall numbers are not reused.
 */

enum syscall_number {
    SYSCALL_DEBUG_PUTCHAR = 1,
    SYSCALL_EXIT = 2,
    SYSCALL_READ_CHAR = 3,
    SYSCALL_READ_EVENT = 4,
    SYSCALL_CLEAR = 5,
    SYSCALL_SET_COLOR = 6,
    SYSCALL_SET_BACKGROUND = 7,
    SYSCALL_RESET_COLORS = 8,
    SYSCALL_MEMORY_STATUS = 9,
    SYSCALL_DEBUG_WRITE = 10,
    SYSCALL_USERSPACE_STATUS = 11,
    SYSCALL_SBRK = 12,
    SYSCALL_KLOG_STATUS = 13,
    SYSCALL_KLOG_READ = 14,
    SYSCALL_TIMER_STATUS = 15,
    SYSCALL_FRAMEBUFFER_STATUS = 16,
    SYSCALL_KERNEL_HEAP_STATUS = 17,
    SYSCALL_APP_COUNT = 18,
    SYSCALL_APP_INFO = 19,
    SYSCALL_APP_RUN = 20,
};

enum user_keyboard_key {
    USER_KEYBOARD_KEY_NONE = 0,
    USER_KEYBOARD_KEY_CHAR,
    USER_KEYBOARD_KEY_ESCAPE,
    USER_KEYBOARD_KEY_BACKSPACE,
    USER_KEYBOARD_KEY_TAB,
    USER_KEYBOARD_KEY_ENTER,
    USER_KEYBOARD_KEY_UP,
    USER_KEYBOARD_KEY_DOWN,
    USER_KEYBOARD_KEY_LEFT,
    USER_KEYBOARD_KEY_RIGHT,
    USER_KEYBOARD_KEY_HOME,
    USER_KEYBOARD_KEY_END,
    USER_KEYBOARD_KEY_DELETE,
};

struct user_input_event {
    enum user_keyboard_key key;
    char ch;
    uint8_t modifiers;
};

struct user_memory_status {
    uint64_t total_kib;
    uint64_t free_kib;
    uint64_t used_kib;
    uint64_t hhdm_offset;
};

struct user_klog_status {
    uint64_t size;
    uint64_t capacity;
    uint64_t total_written;
    uint64_t dropped;
};

struct user_timer_status {
    uint64_t ticks;
    uint32_t frequency_hz;
    uint32_t reserved;
};

struct user_framebuffer_status {
    uint8_t available;
    uint8_t reserved0[7];
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint64_t bpp;
    uint64_t address;
};

struct user_kernel_heap_status {
    uint64_t mapped_bytes;
    uint64_t allocated_bytes;
    uint64_t free_bytes;
    uint8_t valid;
    uint8_t reserved0[7];
};

struct user_userspace_status {
    uint32_t abi_version;
    uint8_t initialized;
    uint8_t syscall_dispatch_ready;
    uint8_t init_process_ready;
    uint8_t scheduler_ready;
    uint8_t preemptive_enabled;
    uint8_t user_task_active;
    uint8_t reserved0[2];
    uint64_t user_min;
    uint64_t user_max;
    uint64_t image_base;
    uint64_t user_stack_top;
    uint64_t kernel_base;
    uint64_t init_entry;
    uint64_t init_pml4;
    uint64_t scheduler_ticks;
    uint64_t user_preemptions;
    uint64_t launches;
    uint64_t exits;
    int64_t last_exit_status;
    uint64_t heap_start;
    uint64_t heap_current;
    uint64_t heap_end;
    uint64_t app_count;
};

struct user_app_info {
    uint32_t abi_version;
    uint32_t index;
    uint64_t image_size;
    uint64_t entry;
    char name[USERSPACE_APP_NAME_SIZE];
};

#endif // WHIRLISCOPE_USERSPACE_ABI_H
