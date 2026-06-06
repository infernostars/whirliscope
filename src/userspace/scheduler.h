#ifndef WHIRLISCOPE_USERSPACE_SCHEDULER_H
#define WHIRLISCOPE_USERSPACE_SCHEDULER_H
#pragma once

#include "arch/x86_64/arch.h"
#include "arch/x86_64/idt.h"
#include "userspace/process.h"
#include <stdbool.h>
#include <stdint.h>

struct scheduler_status {
    bool initialized;
    bool preemptive_enabled;
    bool user_task_active;
    uint64_t ticks;
    uint64_t user_preemptions;
    uint64_t launches;
    uint64_t exits;
    long long last_exit_status;
    process_id_t current_pid;
    enum process_state current_process_state;
    enum thread_state current_thread_state;
};

void scheduler_init(void);
bool scheduler_prepare_launch(struct process *process,
                              struct arch_context *return_context);
__attribute__((noreturn))
void scheduler_enter_current(void);
__attribute__((noreturn))
void scheduler_exit_current(long long status);
void scheduler_tick(struct interrupt_frame *frame);
struct scheduler_status scheduler_get_status(void);

#endif // WHIRLISCOPE_USERSPACE_SCHEDULER_H
