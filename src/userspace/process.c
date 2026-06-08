#include "process.h"

#include "userspace/address_space.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include <libc/mem.h>
#include <libc/string.h>

static struct process processes[USERSPACE_MAX_PROCESSES];
static process_id_t next_pid;

extern const uint8_t user_init_start[];
extern const uint8_t user_init_end[];
extern const uint8_t user_hello_start[];
extern const uint8_t user_hello_end[];

static const struct embedded_user_app embedded_apps[] = {
    {
        .name = "init",
        .start = user_init_start,
        .end = user_init_end,
    },
    {
        .name = "hello",
        .start = user_hello_start,
        .end = user_hello_end,
    },
};

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t segment_map_flags(const struct elf64_phdr *phdr) {
    uint64_t flags = VMM_USER;

    if ((phdr->p_flags & ELF64_PF_W) != 0) {
        flags |= VMM_WRITABLE;
    }
    if ((phdr->p_flags & ELF64_PF_X) == 0) {
        flags |= VMM_NOEXEC;
    }

    return flags;
}

static bool load_segment_page(uint64_t pml4_phys, const uint8_t *image,
                              const struct elf64_phdr *phdr,
                              uint64_t page_vaddr) {
    uint64_t page_phys = pmm_alloc_page();
    if (page_phys == 0) {
        return false;
    }

    uint8_t *page = pmm_phys_to_virt(page_phys);
    memset(page, 0, VMM_PAGE_SIZE);

    uint64_t file_start = phdr->p_vaddr;
    uint64_t file_end = phdr->p_vaddr + phdr->p_filesz;
    uint64_t page_end = page_vaddr + VMM_PAGE_SIZE;
    uint64_t copy_start = page_vaddr > file_start ? page_vaddr : file_start;
    uint64_t copy_end = page_end < file_end ? page_end : file_end;

    if (copy_start < copy_end) {
        uint64_t page_offset = copy_start - page_vaddr;
        uint64_t file_offset = phdr->p_offset + copy_start - phdr->p_vaddr;
        memcpy(page + page_offset, image + file_offset, copy_end - copy_start);
    }

    return vmm_map_page_in_space(pml4_phys, page_vaddr, page_phys,
                                 segment_map_flags(phdr));
}

static bool load_user_segment(uint64_t pml4_phys, const uint8_t *image,
                              const struct elf64_phdr *phdr) {
    if (phdr->p_type != ELF64_PT_LOAD) {
        return true;
    }

    uint64_t segment_end = phdr->p_vaddr + phdr->p_memsz;
    if (segment_end < phdr->p_vaddr) {
        return false;
    }

    uint64_t first_page = align_down(phdr->p_vaddr, VMM_PAGE_SIZE);
    uint64_t end_page = align_up(segment_end, VMM_PAGE_SIZE);
    for (uint64_t page_vaddr = first_page;
         page_vaddr < end_page;
         page_vaddr += VMM_PAGE_SIZE) {
        if (!load_segment_page(pml4_phys, image, phdr, page_vaddr)) {
            return false;
        }
    }

    return true;
}

static bool load_user_image_segments(uint64_t pml4_phys, const uint8_t *image,
                                     const struct elf64_image *elf) {
    for (uint16_t i = 0; i < elf->phnum; i++) {
        const struct elf64_phdr *phdr =
            (const struct elf64_phdr *)(const void *)
            (image + elf->phoff + (uint64_t)elf->phentsize * i);

        if (!load_user_segment(pml4_phys, image, phdr)) {
            return false;
        }
    }

    return true;
}

void process_table_init(void) {
    memset(processes, 0, sizeof(processes));
    next_pid = 1;
}

struct process *process_first(void) {
    return processes[0].state == PROCESS_EMPTY ? NULL : &processes[0];
}

size_t process_embedded_app_count(void) {
    return sizeof(embedded_apps) / sizeof(embedded_apps[0]);
}

const struct embedded_user_app *process_embedded_app_at(size_t index) {
    if (index >= process_embedded_app_count()) {
        return NULL;
    }

    return &embedded_apps[index];
}

bool process_prepare_user_image(struct process *process, process_id_t pid,
                                const char *name, uint64_t pml4_phys,
                                const struct elf64_image *image) {
    if (process == NULL || image == NULL || pml4_phys == 0) {
        return false;
    }

    if (!userspace_range_is_valid(image->entry, 1)
     || !userspace_range_is_valid(USERSPACE_STACK_TOP - 1, 1)) {
        return false;
    }

    memset(process, 0, sizeof(*process));
    process->pid = pid;
    process->state = PROCESS_NEW;
    process->pml4_phys = pml4_phys;
    process->entry = image->entry;
    process->user_stack_top = USERSPACE_STACK_TOP;
    process->heap_start = USERSPACE_HEAP_BASE;
    process->heap_current = USERSPACE_HEAP_BASE;
    process->heap_end = USERSPACE_HEAP_LIMIT;
    strncpy(process->name, name != NULL ? name : "user", PROCESS_NAME_SIZE - 1);
    process->name[PROCESS_NAME_SIZE - 1] = '\0';

    process->main_thread.tid = 1;
    process->main_thread.owner_pid = pid;
    process->main_thread.state = THREAD_NEW;
    process->main_thread.regs.rip = image->entry;
    process->main_thread.regs.rsp = USERSPACE_STACK_TOP;
    process->main_thread.regs.rflags = 0x202;
    return true;
}

static bool process_load_embedded_app(struct process *process,
                                      process_id_t pid,
                                      size_t app_index) {
    const struct embedded_user_app *app = process_embedded_app_at(app_index);
    if (process == NULL || app == NULL) {
        return false;
    }

    uint64_t pml4_phys = vmm_create_address_space();
    uint64_t stack_phys = pmm_alloc_page();
    if (pml4_phys == 0 || stack_phys == 0) {
        return false;
    }

    void *stack_page = pmm_phys_to_virt(stack_phys);
    size_t image_size = (size_t)(app->end - app->start);
    struct elf64_image image;
    if (!elf64_validate_user_image(app->start, image_size, &image)) {
        return false;
    }

    memset(stack_page, 0, VMM_PAGE_SIZE);

    if (!load_user_image_segments(pml4_phys, app->start, &image)
     || !vmm_map_page_in_space(pml4_phys, USERSPACE_STACK_TOP - VMM_PAGE_SIZE,
                               stack_phys,
                               VMM_USER | VMM_WRITABLE | VMM_NOEXEC)) {
        return false;
    }

    if (!process_prepare_user_image(process, pid, app->name, pml4_phys,
                                    &image)) {
        return false;
    }

    process->state = PROCESS_READY;
    process->main_thread.state = THREAD_READY;
    return true;
}

bool process_create_embedded_app(size_t app_index) {
    return process_load_embedded_app(&processes[0], next_pid++, app_index);
}

bool process_create_embedded_demo(void) {
    return process_create_embedded_app(0);
}

struct process *process_create_embedded_process(size_t app_index) {
    for (size_t i = 1; i < USERSPACE_MAX_PROCESSES; i++) {
        if (processes[i].state == PROCESS_EMPTY
         || processes[i].state == PROCESS_EXITED) {
            if (!process_load_embedded_app(&processes[i], next_pid++, app_index)) {
                processes[i].state = PROCESS_EMPTY;
                return NULL;
            }
            return &processes[i];
        }
    }

    return NULL;
}

bool process_sbrk(struct process *process, int64_t increment,
                  uint64_t *old_break) {
    if (process == NULL || old_break == NULL) {
        return false;
    }

    *old_break = process->heap_current;
    if (increment == 0) {
        return true;
    }

    if (increment < 0) {
        return false;
    }

    uint64_t amount = (uint64_t)increment;
    uint64_t new_break = process->heap_current + amount;
    if (new_break < process->heap_current || new_break > process->heap_end) {
        return false;
    }

    uint64_t old_page_end = align_up(process->heap_current, VMM_PAGE_SIZE);
    uint64_t new_page_end = align_up(new_break, VMM_PAGE_SIZE);
    for (uint64_t vaddr = old_page_end; vaddr < new_page_end;
         vaddr += VMM_PAGE_SIZE) {
        uint64_t page_phys = pmm_alloc_page();
        if (page_phys == 0) {
            return false;
        }

        void *page = pmm_phys_to_virt(page_phys);
        memset(page, 0, VMM_PAGE_SIZE);
        if (!vmm_map_page_in_space(process->pml4_phys, vaddr, page_phys,
                                   VMM_USER | VMM_WRITABLE | VMM_NOEXEC)) {
            return false;
        }
    }

    process->heap_current = new_break;
    return true;
}

const char *process_state_name(enum process_state state) {
    switch (state) {
        case PROCESS_EMPTY:
            return "empty";
        case PROCESS_NEW:
            return "new";
        case PROCESS_READY:
            return "ready";
        case PROCESS_RUNNING:
            return "running";
        case PROCESS_EXITED:
            return "exited";
        default:
            return "unknown";
    }
}

const char *thread_state_name(enum thread_state state) {
    switch (state) {
        case THREAD_EMPTY:
            return "empty";
        case THREAD_NEW:
            return "new";
        case THREAD_READY:
            return "ready";
        case THREAD_RUNNING:
            return "running";
        case THREAD_BLOCKED:
            return "blocked";
        case THREAD_EXITED:
            return "exited";
        default:
            return "unknown";
    }
}
