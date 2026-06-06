#include "idt.h"

#include "pic.h"
#include "libc/stdio.h"
#include "userspace/syscall.h"
#include <stddef.h>
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_KERNEL_CODE 0x08
#define IDT_INTERRUPT_GATE 0x8e
#define IDT_USER_INTERRUPT_GATE 0xee
#define SYSCALL_VECTOR 0x80

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

extern void x86_64_load_idt(const struct idt_ptr *idt_ptr);

#define EXTERN_ISR(N) extern void isr##N(void)
EXTERN_ISR(0);  EXTERN_ISR(1);  EXTERN_ISR(2);  EXTERN_ISR(3);
EXTERN_ISR(4);  EXTERN_ISR(5);  EXTERN_ISR(6);  EXTERN_ISR(7);
EXTERN_ISR(8);  EXTERN_ISR(9);  EXTERN_ISR(10); EXTERN_ISR(11);
EXTERN_ISR(12); EXTERN_ISR(13); EXTERN_ISR(14); EXTERN_ISR(15);
EXTERN_ISR(16); EXTERN_ISR(17); EXTERN_ISR(18); EXTERN_ISR(19);
EXTERN_ISR(20); EXTERN_ISR(21); EXTERN_ISR(22); EXTERN_ISR(23);
EXTERN_ISR(24); EXTERN_ISR(25); EXTERN_ISR(26); EXTERN_ISR(27);
EXTERN_ISR(28); EXTERN_ISR(29); EXTERN_ISR(30); EXTERN_ISR(31);
EXTERN_ISR(32); EXTERN_ISR(33); EXTERN_ISR(34); EXTERN_ISR(35);
EXTERN_ISR(36); EXTERN_ISR(37); EXTERN_ISR(38); EXTERN_ISR(39);
EXTERN_ISR(40); EXTERN_ISR(41); EXTERN_ISR(42); EXTERN_ISR(43);
EXTERN_ISR(44); EXTERN_ISR(45); EXTERN_ISR(46); EXTERN_ISR(47);
EXTERN_ISR(128);
#undef EXTERN_ISR

static struct idt_entry idt[IDT_ENTRIES];

static void (*const exception_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static void (*const irq_stubs[16])(void) = {
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
    isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47,
};

static irq_handler_t irq_handlers[16];

static const char *const exception_names[32] = {
    "divide error",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid tss",
    "segment not present",
    "stack segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "simd floating-point exception",
    "virtualization exception",
    "control protection exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection exception",
    "vmm communication exception",
    "security exception",
    "reserved",
};

static void idt_set_gate(size_t vector, void (*handler)(void),
                         uint8_t attributes) {
    uint64_t address = (uint64_t)handler;

    idt[vector] = (struct idt_entry) {
        .offset_low = address & 0xffff,
        .selector = IDT_KERNEL_CODE,
        .ist = 0,
        .attributes = attributes,
        .offset_mid = (address >> 16) & 0xffff,
        .offset_high = (address >> 32) & 0xffffffff,
        .zero = 0,
    };
}

static uint64_t read_cr2(void) {
    uint64_t cr2;
    asm volatile ("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void idt_init(void) {
    for (size_t i = 0; i < 32; i++) {
        idt_set_gate(i, exception_stubs[i], IDT_INTERRUPT_GATE);
    }

    for (size_t i = 0; i < 16; i++) {
        idt_set_gate(PIC_REMAP_OFFSET + i, irq_stubs[i], IDT_INTERRUPT_GATE);
        irq_handlers[i] = NULL;
    }
    idt_set_gate(SYSCALL_VECTOR, isr128, IDT_USER_INTERRUPT_GATE);

    struct idt_ptr idt_ptr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)idt,
    };
    x86_64_load_idt(&idt_ptr);
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void exception_dispatch(struct interrupt_frame *frame) {
    if (frame->vector == SYSCALL_VECTOR) {
        struct syscall_frame syscall = {
            .number = frame->rax,
            .arg0 = frame->rdi,
            .arg1 = frame->rsi,
            .arg2 = frame->rdx,
            .arg3 = frame->r10,
            .arg4 = frame->r8,
            .arg5 = frame->r9,
            .user_rip = frame->rip,
            .user_rsp = frame->user_rsp,
            .rflags = frame->rflags,
        };
        frame->rax = (uint64_t)syscall_dispatch(&syscall);
        frame->rdi = syscall.arg0;
        frame->rsi = syscall.arg1;
        frame->rdx = syscall.arg2;
        frame->r10 = syscall.arg3;
        frame->r8 = syscall.arg4;
        frame->r9 = syscall.arg5;
        return;
    }

    const char *name = frame->vector < 32
        ? exception_names[frame->vector]
        : "unknown interrupt";

    printf("\nCPU exception %llu: %s\n",
           (unsigned long long)frame->vector, name);
    printf("  error=%#llx rip=%#llx cs=%#llx rflags=%#llx\n",
           (unsigned long long)frame->error_code,
           (unsigned long long)frame->rip,
           (unsigned long long)frame->cs,
           (unsigned long long)frame->rflags);
    if (frame->vector == 14) {
        printf("  cr2=%#llx\n", (unsigned long long)read_cr2());
    }
    printf("  rax=%#llx rbx=%#llx rcx=%#llx rdx=%#llx\n",
           (unsigned long long)frame->rax,
           (unsigned long long)frame->rbx,
           (unsigned long long)frame->rcx,
           (unsigned long long)frame->rdx);
    printf("  rsi=%#llx rdi=%#llx rbp=%#llx\n",
           (unsigned long long)frame->rsi,
           (unsigned long long)frame->rdi,
           (unsigned long long)frame->rbp);
    printf("  r8 =%#llx r9 =%#llx r10=%#llx r11=%#llx\n",
           (unsigned long long)frame->r8,
           (unsigned long long)frame->r9,
           (unsigned long long)frame->r10,
           (unsigned long long)frame->r11);
    printf("  r12=%#llx r13=%#llx r14=%#llx r15=%#llx\n",
           (unsigned long long)frame->r12,
           (unsigned long long)frame->r13,
           (unsigned long long)frame->r14,
           (unsigned long long)frame->r15);

    for (;;) {
        asm volatile ("cli; hlt");
    }
}

void irq_dispatch(struct interrupt_frame *frame) {
    uint64_t vector = frame->vector;
    if (vector < PIC_REMAP_OFFSET || vector >= PIC_REMAP_OFFSET + 16) {
        return;
    }

    uint8_t irq = vector - PIC_REMAP_OFFSET;
    if (irq_handlers[irq] != NULL) {
        irq_handlers[irq](frame);
    }

    pic_send_eoi(irq);
}
