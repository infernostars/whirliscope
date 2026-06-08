#ifndef WHIRLISCOPE_USERSPACE_SYSCALL_H
#define WHIRLISCOPE_USERSPACE_SYSCALL_H
#pragma once

#include "userspace/abi.h"
#include <stdbool.h>
#include <stdint.h>

#define SYSCALL_ERR_NOSYS ((long long)USER_ERR_NOSYS)
#define SYSCALL_ERR_AGAIN ((long long)USER_ERR_AGAIN)
#define SYSCALL_ERR_FAULT ((long long)USER_ERR_FAULT)
#define SYSCALL_ERR_INVAL ((long long)USER_ERR_INVAL)

struct syscall_frame {
    uint64_t number;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rbp;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t user_rip;
    uint64_t user_rsp;
    uint64_t rflags;
};

void syscall_init(void);
bool syscall_dispatch_ready(void);
long long syscall_dispatch(struct syscall_frame *frame);

#endif // WHIRLISCOPE_USERSPACE_SYSCALL_H
