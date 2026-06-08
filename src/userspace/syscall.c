#include "syscall.h"

#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "kernel/boot_info.h"
#include "kernel/console.h"
#include "kernel/klog.h"
#include "userspace/address_space.h"
#include "memory/heap.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "userspace/scheduler.h"
#include "userspace/userspace.h"
#include <libc/mem.h>
#include <libc/string.h>

static bool dispatch_ready;

#define USER_WRITE_CHUNK 1024u
#define USER_KLOG_READ_MAX 4096u

static char klog_read_buffer[USER_KLOG_READ_MAX];

void syscall_init(void) {
    dispatch_ready = true;
}

bool syscall_dispatch_ready(void) {
    return dispatch_ready;
}

static bool read_user_bytes(uint64_t user_address, void *dest, size_t len) {
    uint8_t *out = dest;

    for (size_t copied = 0; copied < len; ) {
        uint64_t physical_address;
        if (!vmm_virt_to_phys(user_address + copied, &physical_address)) {
            return false;
        }

        size_t page_bytes = VMM_PAGE_SIZE
            - (size_t)(physical_address & (VMM_PAGE_SIZE - 1));
        size_t chunk = len - copied < page_bytes ? len - copied : page_bytes;

        memcpy(out + copied, pmm_phys_to_virt(physical_address), chunk);
        copied += chunk;
    }

    return true;
}

static bool write_user_bytes(uint64_t user_address, const void *src,
                             size_t len) {
    const uint8_t *in = src;

    for (size_t copied = 0; copied < len; ) {
        uint64_t physical_address;
        if (!vmm_virt_to_phys(user_address + copied, &physical_address)) {
            return false;
        }

        size_t page_bytes = VMM_PAGE_SIZE
            - (size_t)(physical_address & (VMM_PAGE_SIZE - 1));
        size_t chunk = len - copied < page_bytes ? len - copied : page_bytes;

        memcpy(pmm_phys_to_virt(physical_address), in + copied, chunk);
        copied += chunk;
    }

    return true;
}

static bool copy_to_user(uint64_t user_address, const void *src, size_t len) {
    if (len == 0) {
        return true;
    }

    if (!userspace_range_is_valid(user_address, len)) {
        return false;
    }

    return write_user_bytes(user_address, src, len);
}

static long long write_user_string(uint64_t user_address, size_t len) {
    if (len == 0) {
        return 0;
    }

    if (!userspace_range_is_valid(user_address, len)) {
        return SYSCALL_ERR_FAULT;
    }

    char buffer[USER_WRITE_CHUNK];
    size_t written = 0;

    while (written < len) {
        size_t chunk = len - written;
        if (chunk > sizeof(buffer)) {
            chunk = sizeof(buffer);
        }

        if (!read_user_bytes(user_address + written, buffer, chunk)) {
            return SYSCALL_ERR_FAULT;
        }

        console_write(buffer, chunk);
        written += chunk;
    }

    return (long long)len;
}

static struct user_userspace_status make_user_userspace_status(void) {
    struct userspace_status status = userspace_get_status();
    struct process *current = scheduler_current_process();

    return (struct user_userspace_status) {
        .abi_version = USERSPACE_STATUS_ABI_VERSION,
        .initialized = status.initialized,
        .syscall_dispatch_ready = status.syscall_dispatch_ready,
        .init_process_ready = status.init_process_ready,
        .scheduler_ready = status.scheduler_ready,
        .preemptive_enabled = status.preemptive_enabled,
        .user_task_active = status.user_task_active,
        .user_min = status.user_min,
        .user_max = status.user_max,
        .image_base = status.image_base,
        .user_stack_top = status.user_stack_top,
        .kernel_base = status.kernel_base,
        .init_entry = status.init_entry,
        .init_pml4 = status.init_pml4,
        .scheduler_ticks = status.scheduler_ticks,
        .user_preemptions = status.user_preemptions,
        .launches = status.launches,
        .exits = status.exits,
        .last_exit_status = status.last_exit_status,
        .heap_start = current != NULL ? current->heap_start : 0,
        .heap_current = current != NULL ? current->heap_current : 0,
        .heap_end = current != NULL ? current->heap_end : 0,
        .app_count = status.app_count,
    };
}

long long syscall_dispatch(struct syscall_frame *frame) {
    if (frame == NULL) {
        return SYSCALL_ERR_NOSYS;
    }

    switch (frame->number) {
        case SYSCALL_DEBUG_PUTCHAR:
            console_putchar((char)frame->arg0);
            return 0;
        case SYSCALL_EXIT:
            console_write_string("[user exit]\n");
            long long status = (long long)frame->arg0;
            if (scheduler_finish_blocking_exit(status, frame)) {
                return status;
            }
            scheduler_exit_current(status);
        case SYSCALL_READ_CHAR: {
            char ch;
            if (!keyboard_read_char(&ch)) {
                return SYSCALL_ERR_AGAIN;
            }
            return (unsigned char)ch;
        }
        case SYSCALL_READ_EVENT: {
            struct keyboard_event event;
            if (!keyboard_read_event(&event)) {
                return SYSCALL_ERR_AGAIN;
            }

            return (uint64_t)event.key
                 | ((uint64_t)(uint8_t)event.ch << 8)
                 | ((uint64_t)event.modifiers << 16);
        }
        case SYSCALL_CLEAR:
            console_clear();
            return 0;
        case SYSCALL_SET_COLOR:
            console_set_color((uint8_t)frame->arg0, (uint8_t)frame->arg1,
                              (uint8_t)frame->arg2);
            return 0;
        case SYSCALL_SET_BACKGROUND:
            console_set_background((uint8_t)frame->arg0,
                                   (uint8_t)frame->arg1,
                                   (uint8_t)frame->arg2);
            return 0;
        case SYSCALL_RESET_COLORS:
            console_set_default_colors();
            return 0;
        case SYSCALL_MEMORY_STATUS: {
            struct pmm_stats memory = pmm_get_stats();
            struct user_memory_status status = {
                .total_kib = memory.total_bytes / 1024,
                .free_kib = memory.free_bytes / 1024,
                .used_kib = memory.used_bytes / 1024,
                .hhdm_offset = pmm_hhdm_offset(),
            };
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_DEBUG_WRITE:
            return write_user_string(frame->arg0, (size_t)frame->arg1);
        case SYSCALL_USERSPACE_STATUS: {
            struct user_userspace_status status = make_user_userspace_status();
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_SBRK: {
            struct process *current = scheduler_current_process();
            uint64_t old_break;
            if (!process_sbrk(current, (int64_t)frame->arg0, &old_break)) {
                return SYSCALL_ERR_FAULT;
            }
            return (long long)old_break;
        }
        case SYSCALL_KLOG_STATUS: {
            struct user_klog_status status = {
                .size = klog_size(),
                .capacity = KLOG_CAPACITY,
                .total_written = klog_total_written(),
                .dropped = klog_dropped(),
            };
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_KLOG_READ: {
            size_t requested = (size_t)frame->arg1;
            if (requested > sizeof(klog_read_buffer)) {
                requested = sizeof(klog_read_buffer);
            }

            size_t copied = klog_copy_tail(klog_read_buffer, requested);
            if (!copy_to_user(frame->arg0, klog_read_buffer, copied)) {
                return SYSCALL_ERR_FAULT;
            }
            return (long long)copied;
        }
        case SYSCALL_TIMER_STATUS: {
            struct user_timer_status status = {
                .ticks = timer_ticks(),
                .frequency_hz = timer_frequency(),
            };
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_FRAMEBUFFER_STATUS: {
            struct user_framebuffer_status status;
            boot_info_get_framebuffer_status(&status);
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_KERNEL_HEAP_STATUS: {
            struct heap_stats heap = kheap_get_stats();
            struct user_kernel_heap_status status = {
                .mapped_bytes = heap.mapped_bytes,
                .allocated_bytes = heap.allocated_bytes,
                .free_bytes = heap.free_bytes,
                .valid = kheap_validate(),
            };
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_APP_COUNT:
            return (long long)process_embedded_app_count();
        case SYSCALL_APP_INFO: {
            const struct embedded_user_app *app =
                process_embedded_app_at((size_t)frame->arg0);
            if (app == NULL) {
                return SYSCALL_ERR_INVAL;
            }

            struct elf64_image image;
            size_t image_size = (size_t)(app->end - app->start);
            if (!elf64_validate_user_image(app->start, image_size, &image)) {
                return SYSCALL_ERR_FAULT;
            }

            struct user_app_info info = {
                .abi_version = USERSPACE_APP_INFO_ABI_VERSION,
                .index = (uint32_t)frame->arg0,
                .image_size = image_size,
                .entry = image.entry,
            };
            strncpy(info.name, app->name, sizeof(info.name) - 1);
            info.name[sizeof(info.name) - 1] = '\0';

            if (!copy_to_user(frame->arg1, &info, sizeof(info))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_APP_RUN: {
            struct process *process =
                process_create_embedded_process((size_t)frame->arg0);
            if (process == NULL) {
                return SYSCALL_ERR_INVAL;
            }

            scheduler_run_process_blocking(process, frame);
        }
        default:
            return SYSCALL_ERR_NOSYS;
    }
}
