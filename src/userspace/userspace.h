#ifndef WHIRLISCOPE_USERSPACE_H
#define WHIRLISCOPE_USERSPACE_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct userspace_status {
    bool initialized;
    bool syscall_dispatch_ready;
    bool init_process_ready;
    bool scheduler_ready;
    bool preemptive_enabled;
    bool user_task_active;
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
    long long last_exit_status;
    uint64_t app_count;
};

void userspace_init(void);
struct userspace_status userspace_get_status(void);
bool userspace_can_launch_init(void);
void userspace_launch_init(void);

#endif // WHIRLISCOPE_USERSPACE_H
