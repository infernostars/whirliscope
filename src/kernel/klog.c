#include "klog.h"

static char ring[KLOG_CAPACITY];
static size_t head;
static size_t used;
static uint64_t total;

void klog_putchar(char ch) {
    ring[head] = ch;
    head = (head + 1) % KLOG_CAPACITY;

    if (used < KLOG_CAPACITY) {
        used++;
    }

    total++;
}

void klog_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        klog_putchar(s[i]);
    }
}

size_t klog_size(void) {
    return used;
}

uint64_t klog_total_written(void) {
    return total;
}

uint64_t klog_dropped(void) {
    return total > used ? total - used : 0;
}

size_t klog_copy_tail(char *dest, size_t dest_size) {
    if (dest_size == 0 || used == 0) {
        return 0;
    }

    size_t to_copy = used < dest_size ? used : dest_size;
    size_t oldest = (head + KLOG_CAPACITY - used) % KLOG_CAPACITY;
    size_t start = (oldest + used - to_copy) % KLOG_CAPACITY;

    for (size_t i = 0; i < to_copy; i++) {
        dest[i] = ring[(start + i) % KLOG_CAPACITY];
    }

    return to_copy;
}
