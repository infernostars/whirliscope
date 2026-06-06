#include "syscall.h"

#include "drivers/keyboard.h"
#include "kernel/console.h"
#include "memory/pmm.h"
#include "userspace/scheduler.h"

static bool dispatch_ready;

void syscall_init(void) {
    dispatch_ready = true;
}

bool syscall_dispatch_ready(void) {
    return dispatch_ready;
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
        default:
            return SYSCALL_ERR_NOSYS;
    }
}
