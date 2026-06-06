#include "panic.h"

#include "arch/x86_64/arch.h"
#include "kernel/debug.h"
#include "libc/stdio.h"
#include "kernel/console.h"

void vpanic_at(const char *file, int line, const char *fmt, va_list args) {
    console_set_color(255, 0, 0);
    printf("\npanic at %s:%d: ", file, line);
    vprintf(fmt, args);
    printf("\n");

    breakpoint();
    arch_halt();
}

void panic_at(const char *file, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vpanic_at(file, line, fmt, args);
}
