#include "heap.h"

#include "kernel/panic.h"
#include "libc/mem.h"
#include "pmm.h"
#include "vmm.h"
#include <stdbool.h>
#include <stdint.h>

#define KHEAP_BASE 0xffffc00000000000ull
#define KHEAP_MAX_SIZE (64ull * 1024ull * 1024ull)
#define KHEAP_CHUNK_SIZE (64ull * 1024ull)
#define HEAP_ALIGN 16ull
#define HEAP_MAGIC 0x48454150u

struct heap_block {
    uint32_t magic;
    bool free;
    size_t size;
    struct heap_block *prev;
    struct heap_block *next;
};

static struct heap_block *first_block = NULL;
static struct heap_block *last_block = NULL;
static uint64_t heap_start = KHEAP_BASE;
static uint64_t heap_end = KHEAP_BASE;
static size_t mapped_bytes = 0;
static size_t allocated_bytes = 0;
static bool initialized = false;

static size_t align_up_size(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static size_t block_overhead(void) {
    return align_up_size(sizeof(struct heap_block), HEAP_ALIGN);
}

static void *block_payload(struct heap_block *block) {
    return (uint8_t *)block + block_overhead();
}

static struct heap_block *payload_block(void *ptr) {
    return (struct heap_block *)((uint8_t *)ptr - block_overhead());
}

static void append_block(struct heap_block *block) {
    block->prev = last_block;
    block->next = NULL;

    if (last_block != NULL) {
        last_block->next = block;
    } else {
        first_block = block;
    }

    last_block = block;
}

static void split_block(struct heap_block *block, size_t size) {
    size_t overhead = block_overhead();

    if (block->size < size + overhead + HEAP_ALIGN) {
        return;
    }

    struct heap_block *split = (struct heap_block *)((uint8_t *)block_payload(block) + size);
    split->magic = HEAP_MAGIC;
    split->free = true;
    split->size = block->size - size - overhead;
    split->prev = block;
    split->next = block->next;

    if (split->next != NULL) {
        split->next->prev = split;
    } else {
        last_block = split;
    }

    block->size = size;
    block->next = split;
}

static void coalesce_with_next(struct heap_block *block) {
    struct heap_block *next = block->next;

    if (next == NULL || !next->free) {
        return;
    }

    block->size += block_overhead() + next->size;
    block->next = next->next;

    if (block->next != NULL) {
        block->next->prev = block;
    } else {
        last_block = block;
    }
}

static void rollback_heap_mapping(uint64_t start, size_t bytes) {
    for (size_t offset = 0; offset < bytes; offset += VMM_PAGE_SIZE) {
        uint64_t physical = 0;
        if (vmm_virt_to_phys(start + offset, &physical)) {
            vmm_unmap_page(start + offset);
            pmm_free_page(physical);
        }
    }
}

static bool expand_heap(size_t min_payload_size) {
    size_t total_needed = min_payload_size + block_overhead();
    size_t bytes = align_up_size(total_needed, KHEAP_CHUNK_SIZE);

    if (heap_end + bytes < heap_end
     || heap_end + bytes > heap_start + KHEAP_MAX_SIZE) {
        return false;
    }

    uint64_t block_address = heap_end;
    size_t mapped_this_expand = 0;

    for (size_t offset = 0; offset < bytes; offset += VMM_PAGE_SIZE) {
        uint64_t physical = pmm_alloc_page();
        if (physical == 0) {
            rollback_heap_mapping(block_address, mapped_this_expand);
            return false;
        }

        if (!vmm_map_page(heap_end + offset, physical, VMM_WRITABLE)) {
            pmm_free_page(physical);
            rollback_heap_mapping(block_address, mapped_this_expand);
            return false;
        }

        mapped_this_expand += VMM_PAGE_SIZE;
    }

    heap_end += bytes;
    mapped_bytes += bytes;

    struct heap_block *block = (struct heap_block *)block_address;
    block->magic = HEAP_MAGIC;
    block->free = true;
    block->size = bytes - block_overhead();
    append_block(block);

    if (block->prev != NULL && block->prev->free) {
        coalesce_with_next(block->prev);
    }

    return true;
}

static struct heap_block *find_free_block(size_t size) {
    for (struct heap_block *block = first_block; block != NULL; block = block->next) {
        if (block->free && block->size >= size) {
            return block;
        }
    }

    return NULL;
}

void kheap_init(void) {
    first_block = NULL;
    last_block = NULL;
    heap_start = KHEAP_BASE;
    heap_end = KHEAP_BASE;
    mapped_bytes = 0;
    allocated_bytes = 0;
    initialized = true;
}

void *kmalloc(size_t size) {
    if (!initialized || size == 0) {
        return NULL;
    }

    size = align_up_size(size, HEAP_ALIGN);

    struct heap_block *block = find_free_block(size);
    if (block == NULL) {
        if (!expand_heap(size)) {
            return NULL;
        }
        block = find_free_block(size);
    }

    if (block == NULL) {
        return NULL;
    }

    split_block(block, size);
    block->free = false;
    allocated_bytes += block->size;
    return block_payload(block);
}

void *kcalloc(size_t count, size_t size) {
    if (size != 0 && count > ((size_t)-1) / size) {
        return NULL;
    }

    size_t bytes = count * size;
    void *ptr = kmalloc(bytes);
    if (ptr != NULL) {
        memset(ptr, 0, bytes);
    }

    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return kmalloc(size);
    }

    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    struct heap_block *block = payload_block(ptr);
    assert(block->magic == HEAP_MAGIC);

    size_t aligned_size = align_up_size(size, HEAP_ALIGN);
    if (block->size >= aligned_size) {
        size_t old_size = block->size;
        split_block(block, aligned_size);
        if (block->next != NULL && block->next->free) {
            coalesce_with_next(block->next);
        }
        allocated_bytes -= old_size - block->size;
        return ptr;
    }

    void *new_ptr = kmalloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, block->size);
    kfree(ptr);
    return new_ptr;
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct heap_block *block = payload_block(ptr);
    assert(block->magic == HEAP_MAGIC);
    assert(!block->free);

    block->free = true;
    allocated_bytes -= block->size;

    coalesce_with_next(block);
    if (block->prev != NULL && block->prev->free) {
        coalesce_with_next(block->prev);
    }
}

struct heap_stats kheap_get_stats(void) {
    size_t free_bytes = 0;

    for (struct heap_block *block = first_block; block != NULL; block = block->next) {
        if (block->free) {
            free_bytes += block->size;
        }
    }

    return (struct heap_stats) {
        .mapped_bytes = mapped_bytes,
        .allocated_bytes = allocated_bytes,
        .free_bytes = free_bytes,
    };
}

bool kheap_validate(void) {
    if (!initialized) {
        return false;
    }

    size_t calculated_allocated = 0;
    struct heap_block *previous = NULL;

    for (struct heap_block *block = first_block; block != NULL; block = block->next) {
        uint64_t block_address = (uint64_t)block;
        uint64_t payload_address = (uint64_t)block_payload(block);
        uint64_t block_end = payload_address + block->size;

        if (block->magic != HEAP_MAGIC
         || block->prev != previous
         || block_address < heap_start
         || block_address >= heap_end
         || payload_address < block_address
         || block_end < payload_address
         || block_end > heap_end
         || (block->size & (HEAP_ALIGN - 1)) != 0) {
            return false;
        }

        if (block->free && block->next != NULL && block->next->free) {
            return false;
        }

        if (!block->free) {
            calculated_allocated += block->size;
        }

        previous = block;
    }

    if (previous != last_block || calculated_allocated != allocated_bytes) {
        return false;
    }

    return true;
}
