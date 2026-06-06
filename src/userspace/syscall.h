#ifndef WHIRLISCOPE_USERSPACE_SYSCALL_H
#define WHIRLISCOPE_USERSPACE_SYSCALL_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SYSCALL_ERR_NOSYS (-38ll)
#define SYSCALL_ERR_AGAIN (-11ll)

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
};

struct syscall_frame {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t user_rip;
    uint64_t user_rsp;
    uint64_t rflags;
};

void syscall_init(void);
bool syscall_dispatch_ready(void);
long long syscall_dispatch(struct syscall_frame *frame);

#endif // WHIRLISCOPE_USERSPACE_SYSCALL_H
