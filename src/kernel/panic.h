#ifndef WHIRLISCOPE_KERNEL_PANIC_H
#define WHIRLISCOPE_KERNEL_PANIC_H
#pragma once

#include <stdarg.h>

__attribute__((noreturn))
void panic_at(const char *file, int line, const char *fmt, ...);

__attribute__((noreturn))
void vpanic_at(const char *file, int line, const char *fmt, va_list args);

#define panic(...) panic_at(__FILE__, __LINE__, __VA_ARGS__)
#define assert(EXPR) \
    do { \
        if (!(EXPR)) { \
            panic("assertion failed: %s", #EXPR); \
        } \
    } while (0)

#endif // WHIRLISCOPE_KERNEL_PANIC_H
