#include "debug.h"

__attribute__((noinline, used, section(".text.keep.breakpoint")))
void breakpoint(void) {
    // This function intentionally does nothing, but is here to allow GDB to set a software breakpoint.
    asm volatile ("");
}
