#ifndef WHIRLISCOPE_USER_LIBC_STDLIB_H
#define WHIRLISCOPE_USER_LIBC_STDLIB_H
#pragma once

#include <stddef.h>

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

#endif // WHIRLISCOPE_USER_LIBC_STDLIB_H
