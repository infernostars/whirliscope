#include "syscall.h"

#include "drivers/keyboard.h"
#include "kernel/console.h"
#include "libc/mem.h"
#include "userspace/address_space.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "userspace/scheduler.h"

static bool dispatch_ready;

#define USER_WRITE_CHUNK 256u
#define SYSCALL_ERR_FAULT (-14ll)

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
            scheduler_exit_current((long long)frame->arg0);
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
            frame->arg0 = memory.total_bytes / 1024;
            frame->arg1 = memory.free_bytes / 1024;
            frame->arg2 = memory.used_bytes / 1024;
            frame->arg3 = pmm_hhdm_offset();
            return 0;
        }
        case SYSCALL_DEBUG_WRITE:
            return write_user_string(frame->arg0, (size_t)frame->arg1);
        default:
            return SYSCALL_ERR_NOSYS;
    }
}
