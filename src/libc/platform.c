#include "stdio.h"

#if WHIRLISCOPE_USERSPACE

#include "syscall.h"

void whrlibc_write(const char *s, size_t n) {
    user_write(s, n);
}

#else

#include "kernel/console.h"

void whrlibc_write(const char *s, size_t n) {
    console_write(s, n);
}

#endif
