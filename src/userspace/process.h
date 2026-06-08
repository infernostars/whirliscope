#ifndef WHIRLISCOPE_USERSPACE_PROCESS_H
#define WHIRLISCOPE_USERSPACE_PROCESS_H
#pragma once

#include "userspace/elf.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROCESS_NAME_SIZE 32u
#define USERSPACE_MAX_PROCESSES 8u

typedef uint64_t process_id_t;
typedef uint64_t thread_id_t;

struct embedded_user_app {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
};

enum process_state {
    PROCESS_EMPTY,
    PROCESS_NEW,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_EXITED,
};

enum thread_state {
    THREAD_EMPTY,
    THREAD_NEW,
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_EXITED,
};

struct user_registers {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

struct thread {
    thread_id_t tid;
    process_id_t owner_pid;
    enum thread_state state;
    struct user_registers regs;
};

struct process {
    process_id_t pid;
    enum process_state state;
    uint64_t pml4_phys;
    uint64_t entry;
    uint64_t user_stack_top;
    uint64_t heap_start;
    uint64_t heap_current;
    uint64_t heap_end;
    struct thread main_thread;
    char name[PROCESS_NAME_SIZE];
};

void process_table_init(void);
struct process *process_first(void);
struct process *process_create_embedded_process(size_t app_index);
size_t process_embedded_app_count(void);
const struct embedded_user_app *process_embedded_app_at(size_t index);
bool process_prepare_user_image(struct process *process, process_id_t pid,
                                const char *name, uint64_t pml4_phys,
                                const struct elf64_image *image);
bool process_create_embedded_app(size_t app_index);
bool process_create_embedded_demo(void);
bool process_sbrk(struct process *process, int64_t increment,
                  uint64_t *old_break);
const char *process_state_name(enum process_state state);
const char *thread_state_name(enum thread_state state);

#endif // WHIRLISCOPE_USERSPACE_PROCESS_H
