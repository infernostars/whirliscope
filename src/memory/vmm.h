#ifndef WHIRLISCOPE_MEMORY_VMM_H
#define WHIRLISCOPE_MEMORY_VMM_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define VMM_PAGE_SIZE 4096ull
#define VMM_HUGE_2M_SIZE (2ull * 1024ull * 1024ull)
#define VMM_HUGE_1G_SIZE (1024ull * 1024ull * 1024ull)

#define VMM_PRESENT  (1ull << 0)
#define VMM_WRITABLE (1ull << 1)
#define VMM_USER     (1ull << 2)
#define VMM_HUGE     (1ull << 7)
#define VMM_GLOBAL   (1ull << 8)
#define VMM_NOEXEC   (1ull << 63)

void vmm_init(void);
uint64_t vmm_create_address_space(void);
void vmm_activate_address_space(uint64_t pml4_phys);
bool vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                  uint64_t flags);
bool vmm_map_page_in_space(uint64_t pml4_phys, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags);
bool vmm_map_huge_2m(uint64_t virtual_address, uint64_t physical_address,
                     uint64_t flags);
bool vmm_map_huge_1g(uint64_t virtual_address, uint64_t physical_address,
                     uint64_t flags);
bool vmm_unmap_page(uint64_t virtual_address);
bool vmm_virt_to_phys(uint64_t virtual_address, uint64_t *physical_address);
uint64_t vmm_kernel_cr3(void);

#endif // WHIRLISCOPE_MEMORY_VMM_H
