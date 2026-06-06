#ifndef WHIRLISCOPE_ARCH_X86_64_GDT_H
#define WHIRLISCOPE_ARCH_X86_64_GDT_H
#pragma once

#include <stdint.h>

void gdt_init(void);
uint64_t gdt_kernel_stack_top(void);

#endif // WHIRLISCOPE_ARCH_X86_64_GDT_H
