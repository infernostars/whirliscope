#ifndef WHIRLISCOPE_MEMORY_PMM_H
#define WHIRLISCOPE_MEMORY_PMM_H
#pragma once

#include "included/limine.h"
#include <stddef.h>
#include <stdint.h>

#define PMM_PAGE_SIZE 4096ull

struct pmm_stats {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t used_pages;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
};

void pmm_init(struct limine_memmap_response *memmap,
              struct limine_hhdm_response *hhdm);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t physical_address);
void *pmm_phys_to_virt(uint64_t physical_address);
uint64_t pmm_virt_to_phys(const void *virtual_address);
struct pmm_stats pmm_get_stats(void);
uint64_t pmm_hhdm_offset(void);

#endif // WHIRLISCOPE_MEMORY_PMM_H
