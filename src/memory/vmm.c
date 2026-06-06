#include "vmm.h"

#include "arch/x86_64/arch.h"
#include "kernel/panic.h"
#include "libc/mem.h"
#include "pmm.h"
#include <stddef.h>

#define PTE_ADDR_MASK 0x000ffffffffff000ull
#define PTE_HUGE_2M_ADDR_MASK 0x000fffffffe00000ull
#define PTE_HUGE_1G_ADDR_MASK 0x000fffffc0000000ull
#define PTE_TABLE_FLAGS (VMM_PRESENT | VMM_WRITABLE)

static uint64_t kernel_cr3 = 0;
static uint64_t *pml4 = NULL;

struct vmm_mapping {
    uint64_t *entry;
    unsigned int shift;
};

static uint64_t read_cr3(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static void invlpg(uint64_t virtual_address) {
    asm volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

static size_t table_index(uint64_t virtual_address, unsigned int shift) {
    return (virtual_address >> shift) & 0x1ff;
}

static uint64_t *entry_to_table(uint64_t entry) {
    return pmm_phys_to_virt(entry & PTE_ADDR_MASK);
}

static bool is_huge_entry(uint64_t entry, unsigned int shift) {
    return (shift == 30 || shift == 21) && (entry & VMM_HUGE) != 0;
}

static uint64_t *ensure_next_table(uint64_t *table, size_t index,
                                   unsigned int shift, uint64_t flags) {
    if ((table[index] & VMM_PRESENT) != 0) {
        if (is_huge_entry(table[index], shift)) {
            return NULL;
        }
        return entry_to_table(table[index]);
    }

    uint64_t page = pmm_alloc_page();
    if (page == 0) {
        return NULL;
    }

    void *table_virtual = pmm_phys_to_virt(page);
    memset(table_virtual, 0, VMM_PAGE_SIZE);
    table[index] = page | PTE_TABLE_FLAGS | (flags & VMM_USER);
    return table_virtual;
}

static bool walk_to_level_in_root(uint64_t *root, uint64_t virtual_address,
                          bool create, uint64_t flags,
                          unsigned int target_shift,
                          struct vmm_mapping *mapping) {
    assert(root != NULL);

    uint64_t *table = root;
    unsigned int shifts[] = {39, 30, 21, 12};

    for (size_t i = 0; i < 4; i++) {
        unsigned int shift = shifts[i];
        size_t index = table_index(virtual_address, shifts[i]);
        uint64_t *entry = &table[index];

        if (shift == target_shift) {
            mapping->entry = entry;
            mapping->shift = shift;
            return true;
        }

        if ((*entry & VMM_PRESENT) == 0) {
            if (!create) {
                return false;
            }

            table = ensure_next_table(table, index, shift, flags);
        } else if (is_huge_entry(*entry, shift)) {
            mapping->entry = entry;
            mapping->shift = shift;
            return !create;
        } else {
            table = entry_to_table(*entry);
        }

        if (table == NULL) {
            return false;
        }
    }

    return false;
}

static bool walk_to_level(uint64_t virtual_address, bool create,
                          unsigned int target_shift,
                          struct vmm_mapping *mapping) {
    return walk_to_level_in_root(pml4, virtual_address, create, 0,
                                 target_shift, mapping);
}

static bool walk_to_existing_mapping(uint64_t virtual_address,
                                     struct vmm_mapping *mapping) {
    if (!walk_to_level(virtual_address, false, 12, mapping)) {
        return false;
    }

    return (*mapping->entry & VMM_PRESENT) != 0;
}

static bool addresses_are_aligned(uint64_t virtual_address,
                                  uint64_t physical_address,
                                  uint64_t page_size) {
    uint64_t mask = page_size - 1;
    return (virtual_address & mask) == 0 && (physical_address & mask) == 0;
}

static bool map_at_level_in_root(uint64_t *root, uint64_t virtual_address,
                                 uint64_t physical_address,
                                 uint64_t page_size,
                                 unsigned int target_shift,
                                 uint64_t flags) {
    if (!addresses_are_aligned(virtual_address, physical_address, page_size)) {
        return false;
    }

    struct vmm_mapping mapping;
    if (!walk_to_level_in_root(root, virtual_address, true, flags,
                               target_shift, &mapping)) {
        return false;
    }

    if (mapping.shift != target_shift || (*mapping.entry & VMM_PRESENT) != 0) {
        return false;
    }

    uint64_t entry_flags = flags | VMM_PRESENT;
    if (target_shift != 12) {
        entry_flags |= VMM_HUGE;
    }

    *mapping.entry = physical_address | entry_flags;
    if (root == pml4) {
        invlpg(virtual_address);
    }
    return true;
}

static bool map_at_level(uint64_t virtual_address, uint64_t physical_address,
                         uint64_t page_size, unsigned int target_shift,
                         uint64_t flags) {
    return map_at_level_in_root(pml4, virtual_address, physical_address,
                                page_size, target_shift, flags);
}

void vmm_init(void) {
    kernel_cr3 = read_cr3() & PTE_ADDR_MASK;
    pml4 = pmm_phys_to_virt(kernel_cr3);
}

uint64_t vmm_create_address_space(void) {
    assert(pml4 != NULL);

    uint64_t new_pml4_phys = pmm_alloc_page();
    if (new_pml4_phys == 0) {
        return 0;
    }

    uint64_t *new_pml4 = pmm_phys_to_virt(new_pml4_phys);
    memset(new_pml4, 0, VMM_PAGE_SIZE);

    for (size_t i = 256; i < 512; i++) {
        new_pml4[i] = pml4[i];
    }

    return new_pml4_phys;
}

void vmm_activate_address_space(uint64_t pml4_phys) {
    if (pml4_phys == 0) {
        pml4_phys = kernel_cr3;
    }

    arch_write_cr3(pml4_phys);
    pml4 = pmm_phys_to_virt(pml4_phys);
}

bool vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                  uint64_t flags) {
    return map_at_level(virtual_address, physical_address, VMM_PAGE_SIZE, 12,
                        flags);
}

bool vmm_map_page_in_space(uint64_t pml4_phys, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags) {
    if (pml4_phys == 0) {
        return false;
    }

    uint64_t *root = pmm_phys_to_virt(pml4_phys);
    return map_at_level_in_root(root, virtual_address, physical_address,
                                VMM_PAGE_SIZE, 12, flags);
}

bool vmm_map_huge_2m(uint64_t virtual_address, uint64_t physical_address,
                     uint64_t flags) {
    return map_at_level(virtual_address, physical_address, VMM_HUGE_2M_SIZE,
                        21, flags);
}

bool vmm_map_huge_1g(uint64_t virtual_address, uint64_t physical_address,
                     uint64_t flags) {
    return map_at_level(virtual_address, physical_address, VMM_HUGE_1G_SIZE,
                        30, flags);
}

bool vmm_unmap_page(uint64_t virtual_address) {
    if ((virtual_address & (VMM_PAGE_SIZE - 1)) != 0) {
        return false;
    }

    struct vmm_mapping mapping;
    if (!walk_to_existing_mapping(virtual_address, &mapping)) {
        return false;
    }

    if (mapping.shift != 12
     && (virtual_address & ((1ull << mapping.shift) - 1)) != 0) {
        return false;
    }

    *mapping.entry = 0;
    invlpg(virtual_address);
    return true;
}

bool vmm_virt_to_phys(uint64_t virtual_address, uint64_t *physical_address) {
    struct vmm_mapping mapping;
    if (!walk_to_existing_mapping(virtual_address, &mapping)) {
        return false;
    }

    if (physical_address != NULL) {
        uint64_t mask = PTE_ADDR_MASK;
        if (mapping.shift == 30) {
            mask = PTE_HUGE_1G_ADDR_MASK;
        } else if (mapping.shift == 21) {
            mask = PTE_HUGE_2M_ADDR_MASK;
        }

        *physical_address = (*mapping.entry & mask)
            | (virtual_address & ((1ull << mapping.shift) - 1));
    }
    return true;
}

uint64_t vmm_kernel_cr3(void) {
    return kernel_cr3;
}
