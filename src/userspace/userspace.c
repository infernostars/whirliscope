#include "userspace.h"

#include "arch/x86_64/arch.h"
#include "kernel/panic.h"
#include "userspace/address_space.h"
#include "userspace/process.h"
#include "userspace/scheduler.h"
#include "userspace/syscall.h"
#include "memory/vmm.h"

static bool initialized;
static bool init_process_ready;
static struct arch_context init_return_context;

void userspace_init(void) {
    process_table_init();
    scheduler_init();
    syscall_init();
    init_process_ready = process_create_filesystem_init("/bin/init");
    if (!init_process_ready) {
        panic("filesystem /bin/init could not be loaded");
    }
    initialized = true;
}

struct userspace_status userspace_get_status(void) {
    struct process *init = process_first();
    struct scheduler_status scheduler = scheduler_get_status();

    return (struct userspace_status) {
        .initialized = initialized,
        .syscall_dispatch_ready = syscall_dispatch_ready(),
        .init_process_ready = init_process_ready,
        .scheduler_ready = scheduler.initialized,
        .preemptive_enabled = scheduler.preemptive_enabled,
        .user_task_active = scheduler.user_task_active,
        .user_min = USERSPACE_MIN_VADDR,
        .user_max = USERSPACE_MAX_VADDR,
        .image_base = USERSPACE_IMAGE_BASE,
        .user_stack_top = USERSPACE_STACK_TOP,
        .kernel_base = KERNELSPACE_BASE_VADDR,
        .init_entry = init != NULL ? init->entry : 0,
        .init_pml4 = init != NULL ? init->pml4_phys : 0,
        .scheduler_ticks = scheduler.ticks,
        .user_preemptions = scheduler.user_preemptions,
        .launches = scheduler.launches,
        .exits = scheduler.exits,
        .last_exit_status = scheduler.last_exit_status,
        .app_count = 0,
    };
}

bool userspace_can_launch_init(void) {
    return initialized && init_process_ready && process_first() != NULL;
}

void userspace_launch_init(void) {
    struct process *init = process_first();
    if (!userspace_can_launch_init() || init == NULL) {
        panic("userspace init process is not ready");
    }

    if (init->state == PROCESS_EXITED) {
        init_process_ready = process_create_filesystem_init("/bin/init");
        if (!init_process_ready) {
            panic("filesystem /bin/init could not be reloaded");
        }
        init = process_first();
    }

    if (!init_process_ready || init == NULL) {
        panic("failed to recreate userspace init process");
    }

    if (arch_context_save(&init_return_context) != 0) {
        arch_enable_interrupts();
        return;
    }

    if (!scheduler_prepare_launch(init, &init_return_context)) {
        panic("scheduler refused userspace init process");
    }
    scheduler_enter_current();
}
