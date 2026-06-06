#ifndef WHIRLISCOPE_LIBC_STRING_H
#define WHIRLISCOPE_LIBC_STRING_H
#pragma once

#include <stddef.h>

size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *restrict dest, const char *restrict src);
char *strncpy(char *restrict dest, const char *restrict src, size_t n);
char *strchr(const char *s, int c);

#endif // WHIRLISCOPE_LIBC_STRING_H
