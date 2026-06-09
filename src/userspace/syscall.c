#include "syscall.h"

#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "fs/ext2.h"
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
#define USER_FS_PATH_MAX 128u
#define USER_FS_LIST_MAX 32u
#define USER_FS_READ_MAX 4096u

static char klog_read_buffer[USER_KLOG_READ_MAX];
static char fs_path_buffer[USER_FS_PATH_MAX];
static char fs_resolved_path[USER_FS_PATH_MAX];
static struct user_fs_dirent fs_dirent_buffer[USER_FS_LIST_MAX];
static uint8_t fs_read_buffer[USER_FS_READ_MAX];

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

static bool copy_user_string(uint64_t user_address, char *dest,
                             size_t dest_size) {
    if (dest_size == 0) {
        return false;
    }

    for (size_t i = 0; i < dest_size; i++) {
        if (!userspace_range_is_valid(user_address + i, 1)
         || !read_user_bytes(user_address + i, &dest[i], 1)) {
            return false;
        }
        if (dest[i] == '\0') {
            return true;
        }
    }

    dest[dest_size - 1] = '\0';
    return false;
}

static bool resolve_path_for_current(const char *path, char *out,
                                     size_t out_size) {
    if (path == NULL || out == NULL || out_size == 0 || path[0] == '\0') {
        return false;
    }

    struct process *current = scheduler_current_process();
    const char *cwd = current != NULL && current->cwd[0] != '\0'
        ? current->cwd
        : "/";
    char combined[USER_FS_PATH_MAX];
    if (path[0] == '/') {
        strncpy(combined, path, sizeof(combined) - 1);
        combined[sizeof(combined) - 1] = '\0';
    } else {
        size_t cwd_len = strlen(cwd);
        size_t path_len = strlen(path);
        bool needs_slash = cwd_len > 1 && cwd[cwd_len - 1] != '/';
        if (cwd_len + (needs_slash ? 1u : 0u) + path_len + 1u
            > sizeof(combined)) {
            return false;
        }

        strcpy(combined, cwd);
        if (needs_slash) {
            combined[cwd_len++] = '/';
            combined[cwd_len] = '\0';
        }
        strcpy(combined + cwd_len, path);
    }

    if (out_size < 2) {
        return false;
    }
    out[0] = '/';
    out[1] = '\0';
    size_t out_len = 1;

    const char *cursor = combined;
    while (*cursor == '/') {
        cursor++;
    }
    while (*cursor != '\0') {
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        size_t len = (size_t)(cursor - start);

        if (len == 1 && start[0] == '.') {
            // Current-directory components do not affect the resolved path.
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (out_len > 1) {
                while (out_len > 1 && out[out_len - 1] != '/') {
                    out_len--;
                }
                if (out_len > 1) {
                    out_len--;
                }
                out[out_len] = '\0';
            }
        } else if (len != 0) {
            size_t needed = out_len + (out_len > 1 ? 1u : 0u) + len + 1u;
            if (needed > out_size) {
                return false;
            }
            if (out_len > 1) {
                out[out_len++] = '/';
            }
            memcpy(out + out_len, start, len);
            out_len += len;
            out[out_len] = '\0';
        }

        while (*cursor == '/') {
            cursor++;
        }
    }

    if (out_len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return true;
}

static bool copy_and_resolve_user_path(uint64_t user_address) {
    return copy_user_string(user_address, fs_path_buffer, sizeof(fs_path_buffer))
        && resolve_path_for_current(fs_path_buffer, fs_resolved_path,
                                    sizeof(fs_resolved_path));
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
            return 0;
        case SYSCALL_APP_INFO:
        case SYSCALL_APP_RUN:
            return SYSCALL_ERR_INVAL;
        case SYSCALL_APP_RUN_PATH: {
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            struct process *current = scheduler_current_process();
            struct process *process =
                process_create_filesystem_process(fs_resolved_path,
                                                  current != NULL
                                                      ? current->cwd
                                                      : "/");
            if (process == NULL) {
                return SYSCALL_ERR_INVAL;
            }

            scheduler_run_process_blocking(process, frame);
        }
        case SYSCALL_FS_STATUS: {
            struct user_fs_status status;
            ext2_get_status(&status);
            if (!copy_to_user(frame->arg0, &status, sizeof(status))) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_FS_LIST: {
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }

            size_t max_entries = (size_t)frame->arg2;
            if (max_entries > USER_FS_LIST_MAX) {
                max_entries = USER_FS_LIST_MAX;
            }

            long count = ext2_list_dir(fs_resolved_path, fs_dirent_buffer,
                                       max_entries);
            if (count < 0) {
                return count;
            }

            size_t bytes = (size_t)count * sizeof(struct user_fs_dirent);
            if (!copy_to_user(frame->arg1, fs_dirent_buffer, bytes)) {
                return SYSCALL_ERR_FAULT;
            }
            return count;
        }
        case SYSCALL_FS_READ: {
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }

            size_t requested = (size_t)frame->arg2;
            if (requested > USER_FS_READ_MAX) {
                requested = USER_FS_READ_MAX;
            }

            long copied = ext2_read_file(fs_resolved_path, fs_read_buffer,
                                         requested, frame->arg3);
            if (copied < 0) {
                return copied;
            }

            if (!copy_to_user(frame->arg1, fs_read_buffer, (size_t)copied)) {
                return SYSCALL_ERR_FAULT;
            }
            return copied;
        }
        case SYSCALL_FS_GETCWD: {
            struct process *current = scheduler_current_process();
            const char *cwd = current != NULL && current->cwd[0] != '\0'
                ? current->cwd
                : "/";
            size_t len = strlen(cwd) + 1;
            if (len > (size_t)frame->arg1) {
                return SYSCALL_ERR_INVAL;
            }
            if (!copy_to_user(frame->arg0, cwd, len)) {
                return SYSCALL_ERR_FAULT;
            }
            return 0;
        }
        case SYSCALL_FS_CHDIR: {
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            struct user_fs_dirent entry;
            if (ext2_stat(fs_resolved_path, &entry) != 0
             || entry.type != USER_FS_TYPE_DIR) {
                return SYSCALL_ERR_INVAL;
            }
            struct process *current = scheduler_current_process();
            if (!process_set_cwd(current, fs_resolved_path)) {
                return SYSCALL_ERR_INVAL;
            }
            return 0;
        }
        case SYSCALL_FS_WRITE: {
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }

            size_t requested = (size_t)frame->arg2;
            if (requested > USER_FS_READ_MAX) {
                requested = USER_FS_READ_MAX;
            }
            if (!read_user_bytes(frame->arg1, fs_read_buffer, requested)) {
                return SYSCALL_ERR_FAULT;
            }

            return ext2_write_file(fs_resolved_path, fs_read_buffer,
                                   requested, frame->arg3);
        }
        case SYSCALL_FS_CREATE:
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            return ext2_create_file(fs_resolved_path);
        case SYSCALL_FS_MKDIR:
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            return ext2_mkdir(fs_resolved_path);
        case SYSCALL_FS_UNLINK:
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            return ext2_unlink(fs_resolved_path);
        case SYSCALL_FS_RMDIR:
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            return ext2_rmdir(fs_resolved_path);
        case SYSCALL_FS_TRUNCATE:
            if (!copy_and_resolve_user_path(frame->arg0)) {
                return SYSCALL_ERR_FAULT;
            }
            return ext2_truncate_file(fs_resolved_path, frame->arg1);
        default:
            return SYSCALL_ERR_NOSYS;
    }
}
