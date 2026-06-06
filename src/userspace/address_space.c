#include "address_space.h"

bool userspace_range_is_valid(uint64_t vaddr, uint64_t size) {
    if (size == 0 || vaddr < USERSPACE_MIN_VADDR) {
        return false;
    }

    uint64_t end = vaddr + size - 1;
    if (end < vaddr) {
        return false;
    }

    return end <= USERSPACE_MAX_VADDR;
}
