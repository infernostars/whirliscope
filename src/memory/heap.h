#ifndef WHIRLISCOPE_MEMORY_HEAP_H
#define WHIRLISCOPE_MEMORY_HEAP_H
#pragma once

#include <stdbool.h>
#include <stddef.h>

struct heap_stats {
    size_t mapped_bytes;
    size_t allocated_bytes;
    size_t free_bytes;
};

void kheap_init(void);
void *kmalloc(size_t size);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);
struct heap_stats kheap_get_stats(void);
bool kheap_validate(void);

#endif // WHIRLISCOPE_MEMORY_HEAP_H
