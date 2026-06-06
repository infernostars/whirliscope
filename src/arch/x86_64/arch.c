#include "arch.h"

#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include <stdint.h>

extern uint64_t x86_64_read_cr3(void);
extern void x86_64_write_cr3(uint64_t cr3);
extern __attribute__((noreturn)) void x86_64_enter_user(uint64_t entry,
                                                       uint64_t stack_top);

void arch_init(void) {
    gdt_init();
    pic_init();
    idt_init();
}

void arch_enable_interrupts(void) {
    asm volatile ("sti");
}

void arch_disable_interrupts(void) {
    asm volatile ("cli");
}

bool arch_interrupts_enabled(void) {
    uint64_t flags;
    asm volatile ("pushfq; pop %0" : "=r"(flags));
    return (flags & (1ull << 9)) != 0;
}

void arch_wait_for_interrupt(void) {
    asm volatile ("hlt");
}

uint64_t arch_read_cr3(void) {
    return x86_64_read_cr3();
}

void arch_write_cr3(uint64_t cr3) {
    x86_64_write_cr3(cr3);
}

__attribute__((noreturn))
void arch_enter_user(uint64_t entry, uint64_t stack_top) {
    x86_64_enter_user(entry, stack_top);
}

__attribute__((noreturn))
void arch_halt(void) {
    for (;;) {
        asm volatile ("cli; hlt");
    }
}
