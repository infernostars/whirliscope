#include "scheduler.h"

#include "kernel/panic.h"
#include "memory/vmm.h"
#include "userspace/syscall.h"

static struct process *current;
static struct arch_context *kernel_return_context;
static bool initialized;
static bool preemptive_enabled;
static uint64_t ticks;
static uint64_t user_preemptions;
static uint64_t launches;
static uint64_t exits;
static long long last_exit_status;
static bool blocking_run_active;
static struct process *blocked_parent;
static struct user_registers blocked_parent_regs;

static bool frame_is_from_user(const struct interrupt_frame *frame) {
    return frame != NULL && (frame->cs & 3u) == 3u;
}

static void save_user_frame(struct thread *thread,
                            const struct interrupt_frame *frame) {
    thread->regs.rip = frame->rip;
    thread->regs.rsp = frame->user_rsp;
    thread->regs.rflags = frame->rflags;
    thread->regs.rax = frame->rax;
    thread->regs.rbx = frame->rbx;
    thread->regs.rcx = frame->rcx;
    thread->regs.rdx = frame->rdx;
    thread->regs.rsi = frame->rsi;
    thread->regs.rdi = frame->rdi;
    thread->regs.rbp = frame->rbp;
    thread->regs.r8 = frame->r8;
    thread->regs.r9 = frame->r9;
    thread->regs.r10 = frame->r10;
    thread->regs.r11 = frame->r11;
    thread->regs.r12 = frame->r12;
    thread->regs.r13 = frame->r13;
    thread->regs.r14 = frame->r14;
    thread->regs.r15 = frame->r15;
}

void scheduler_init(void) {
    current = NULL;
    kernel_return_context = NULL;
    initialized = true;
    preemptive_enabled = true;
    ticks = 0;
    user_preemptions = 0;
    launches = 0;
    exits = 0;
    last_exit_status = 0;
    blocking_run_active = false;
    blocked_parent = NULL;
    blocked_parent_regs = (struct user_registers) {0};
}

bool scheduler_prepare_launch(struct process *process,
                              struct arch_context *return_context) {
    if (!initialized || process == NULL || return_context == NULL
     || process->state != PROCESS_READY
     || process->main_thread.state != THREAD_READY) {
        return false;
    }

    current = process;
    kernel_return_context = return_context;
    return true;
}

__attribute__((noreturn))
void scheduler_enter_current(void) {
    if (current == NULL || kernel_return_context == NULL) {
        panic("scheduler has no current userspace task");
    }

    launches++;
    current->state = PROCESS_RUNNING;
    current->main_thread.state = THREAD_RUNNING;
    vmm_activate_address_space(current->pml4_phys);
    arch_enter_user(current->main_thread.regs.rip,
                    current->main_thread.regs.rsp);
}

static struct user_registers registers_from_syscall_frame(
    const struct syscall_frame *frame
) {
    return (struct user_registers) {
        .rip = frame->user_rip,
        .rsp = frame->user_rsp,
        .rflags = frame->rflags,
        .rax = 0,
        .rbx = frame->rbx,
        .rcx = frame->rcx,
        .rdx = frame->arg2,
        .rsi = frame->arg1,
        .rdi = frame->arg0,
        .rbp = frame->rbp,
        .r8 = frame->arg4,
        .r9 = frame->arg5,
        .r10 = frame->arg3,
        .r11 = frame->r11,
        .r12 = frame->r12,
        .r13 = frame->r13,
        .r14 = frame->r14,
        .r15 = frame->r15,
    };
}

static void syscall_frame_from_registers(struct syscall_frame *frame,
                                         const struct user_registers *regs) {
    frame->arg0 = regs->rdi;
    frame->arg1 = regs->rsi;
    frame->arg2 = regs->rdx;
    frame->arg3 = regs->r10;
    frame->arg4 = regs->r8;
    frame->arg5 = regs->r9;
    frame->rbx = regs->rbx;
    frame->rcx = regs->rcx;
    frame->rbp = regs->rbp;
    frame->r11 = regs->r11;
    frame->r12 = regs->r12;
    frame->r13 = regs->r13;
    frame->r14 = regs->r14;
    frame->r15 = regs->r15;
    frame->user_rip = regs->rip;
    frame->user_rsp = regs->rsp;
    frame->rflags = regs->rflags;
}

__attribute__((noreturn))
void scheduler_run_process_blocking(struct process *process,
                                    const struct syscall_frame *parent_frame) {
    if (!initialized || current == NULL || process == NULL
     || process->state != PROCESS_READY || process->main_thread.state != THREAD_READY
     || parent_frame == NULL || blocking_run_active) {
        panic("invalid blocking userspace launch");
    }

    struct process *parent = current;
    parent->state = PROCESS_READY;
    parent->main_thread.state = THREAD_BLOCKED;

    blocked_parent = parent;
    blocked_parent_regs = registers_from_syscall_frame(parent_frame);
    blocking_run_active = true;

    current = process;
    scheduler_enter_current();
}

bool scheduler_finish_blocking_exit(long long status,
                                    struct syscall_frame *return_frame) {
    if (!blocking_run_active || blocked_parent == NULL || return_frame == NULL) {
        return false;
    }

    if (current != NULL) {
        current->state = PROCESS_EXITED;
        current->main_thread.state = THREAD_EXITED;
    }

    exits++;
    last_exit_status = status;

    current = blocked_parent;
    current->state = PROCESS_RUNNING;
    current->main_thread.state = THREAD_RUNNING;
    vmm_activate_address_space(current->pml4_phys);
    syscall_frame_from_registers(return_frame, &blocked_parent_regs);

    blocked_parent = NULL;
    blocked_parent_regs = (struct user_registers) {0};
    blocking_run_active = false;
    return true;
}

__attribute__((noreturn))
void scheduler_exit_current(long long status) {
    if (current != NULL) {
        current->state = PROCESS_EXITED;
        current->main_thread.state = THREAD_EXITED;
    }

    exits++;
    last_exit_status = status;
    current = NULL;
    vmm_activate_address_space(0);

    if (kernel_return_context == NULL) {
        panic("userspace exited without a kernel return context");
    }

    arch_context_restore(kernel_return_context, 1);
}

void scheduler_tick(struct interrupt_frame *frame) {
    ticks++;

    if (!preemptive_enabled || current == NULL || !frame_is_from_user(frame)) {
        return;
    }

    save_user_frame(&current->main_thread, frame);
    user_preemptions++;
}

struct scheduler_status scheduler_get_status(void) {
    return (struct scheduler_status) {
        .initialized = initialized,
        .preemptive_enabled = preemptive_enabled,
        .user_task_active = current != NULL,
        .ticks = ticks,
        .user_preemptions = user_preemptions,
        .launches = launches,
        .exits = exits,
        .last_exit_status = last_exit_status,
        .current_pid = current != NULL ? current->pid : 0,
        .current_process_state = current != NULL
            ? current->state
            : PROCESS_EMPTY,
        .current_thread_state = current != NULL
            ? current->main_thread.state
            : THREAD_EMPTY,
    };
}

struct process *scheduler_current_process(void) {
    return current;
}
