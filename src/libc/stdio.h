#ifndef WHIRLISCOPE_LIBC_STDIO_H
#define WHIRLISCOPE_LIBC_STDIO_H
#pragma once

#include <stdarg.h>
#include <stddef.h>

void whrlibc_write(const char *s, size_t n);
int putchar(int ch);
int fputs(const char *s);
int puts(const char *s);
int printf(const char *restrict fmt, ...);
int vprintf(const char *restrict fmt, va_list args);
int snprintf(char *restrict s, size_t n, const char *restrict fmt, ...);
int vsnprintf(char *restrict s, size_t n, const char *restrict fmt, va_list args);

#endif // WHIRLISCOPE_LIBC_STDIO_H
