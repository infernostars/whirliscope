#include "pmm.h"

#include "libc/stdio.h"
#include <stdbool.h>

struct free_page {
    struct free_page *next;
};

static struct free_page *free_list = NULL;
static uint64_t hhdm_base = 0;
static uint64_t total_pages = 0;
static uint64_t free_pages = 0;
static bool initialized = false;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t align_down(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

static void push_page(uint64_t physical_address) {
    struct free_page *page = pmm_phys_to_virt(physical_address);
    page->next = free_list;
    free_list = page;
    free_pages++;
}

void pmm_init(struct limine_memmap_response *memmap,
              struct limine_hhdm_response *hhdm) {
    free_list = NULL;
    hhdm_base = 0;
    total_pages = 0;
    free_pages = 0;
    initialized = false;

    if (memmap == NULL || hhdm == NULL) {
        printf("pmm: missing memmap or hhdm response\n");
        return;
    }

    hhdm_base = hhdm->offset;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start = align_up(entry->base, PMM_PAGE_SIZE);
        uint64_t end = align_down(entry->base + entry->length, PMM_PAGE_SIZE);

        for (uint64_t page = start; page < end; page += PMM_PAGE_SIZE) {
            if (page == 0) {
                continue;
            }

            push_page(page);
            total_pages++;
        }
    }

    initialized = true;
}

uint64_t pmm_alloc_page(void) {
    if (!initialized || free_list == NULL) {
        return 0;
    }

    struct free_page *page = free_list;
    free_list = page->next;
    free_pages--;
    return pmm_virt_to_phys(page);
}

void pmm_free_page(uint64_t physical_address) {
    if (!initialized || physical_address == 0
     || (physical_address & (PMM_PAGE_SIZE - 1)) != 0) {
        return;
    }

    push_page(physical_address);
}

void *pmm_phys_to_virt(uint64_t physical_address) {
    return (void *)(hhdm_base + physical_address);
}

uint64_t pmm_virt_to_phys(const void *virtual_address) {
    return (uint64_t)virtual_address - hhdm_base;
}

struct pmm_stats pmm_get_stats(void) {
    uint64_t used = total_pages - free_pages;

    return (struct pmm_stats) {
        .total_pages = total_pages,
        .free_pages = free_pages,
        .used_pages = used,
        .total_bytes = total_pages * PMM_PAGE_SIZE,
        .free_bytes = free_pages * PMM_PAGE_SIZE,
        .used_bytes = used * PMM_PAGE_SIZE,
    };
}

uint64_t pmm_hhdm_offset(void) {
    return hhdm_base;
}
