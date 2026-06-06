#ifndef WHIRLISCOPE_KLOG_H
#define WHIRLISCOPE_KLOG_H
#pragma once

#include <stddef.h>
#include <stdint.h>

#define KLOG_CAPACITY 8192u

void klog_putchar(char ch);
void klog_write(const char *s, size_t n);
size_t klog_size(void);
uint64_t klog_total_written(void);
uint64_t klog_dropped(void);
size_t klog_copy_tail(char *dest, size_t dest_size);

#endif // WHIRLISCOPE_KLOG_H
