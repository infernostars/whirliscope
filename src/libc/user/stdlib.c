#include "stdlib.h"

#include <libc/string.h>
#include "syscall.h"
#include <stdint.h>

#define USER_MALLOC_ALIGN 16ull

struct allocation_header {
    size_t size;
};

static size_t align_up_size(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size_t aligned = align_up_size(size, USER_MALLOC_ALIGN);
    size_t total = sizeof(struct allocation_header) + aligned;
    struct allocation_header *header = user_sbrk((int64_t)total);
    if (header == NULL) {
        return NULL;
    }

    header->size = aligned;
    return header + 1;
}

void free(void *ptr) {
    (void)ptr;
}

void *calloc(size_t nmemb, size_t size) {
    if (size != 0 && nmemb > SIZE_MAX / size) {
        return NULL;
    }

    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return NULL;
    }

    struct allocation_header *old_header =
        ((struct allocation_header *)ptr) - 1;
    void *new_ptr = malloc(size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t to_copy = old_header->size < size ? old_header->size : size;
    memcpy(new_ptr, ptr, to_copy);
    return new_ptr;
}
