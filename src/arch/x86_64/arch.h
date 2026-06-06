#ifndef WHIRLISCOPE_ARCH_X86_64_ARCH_H
#define WHIRLISCOPE_ARCH_X86_64_ARCH_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct arch_context {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
};

void arch_init(void);
void arch_enable_interrupts(void);
void arch_disable_interrupts(void);
bool arch_interrupts_enabled(void);
void arch_wait_for_interrupt(void);
uint64_t arch_read_cr3(void);
void arch_write_cr3(uint64_t cr3);
__attribute__((returns_twice))
int arch_context_save(struct arch_context *context);
__attribute__((noreturn))
void arch_context_restore(struct arch_context *context, int value);
__attribute__((noreturn))
void arch_enter_user(uint64_t entry, uint64_t stack_top);
__attribute__((noreturn))
void arch_halt(void);

#endif // WHIRLISCOPE_ARCH_X86_64_ARCH_H
