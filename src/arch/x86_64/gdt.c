#include "gdt.h"

#include <stdint.h>

#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA 0x18
#define GDT_USER_CODE 0x20
#define GDT_TSS 0x28
#define KERNEL_STACK_SIZE 16384u

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

extern void x86_64_load_gdt(const struct gdt_ptr *gdt_ptr);
extern void x86_64_load_tss(uint16_t selector);

static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static struct tss tss;

static uint64_t gdt[7] = {
    0x0000000000000000,
    0x00af9a000000ffff,
    0x00af92000000ffff,
    0x00aff2000000ffff,
    0x00affa000000ffff,
    0x0000000000000000,
    0x0000000000000000,
};

static void set_tss_descriptor(void) {
    uint64_t base = (uint64_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    gdt[5] = ((uint64_t)(limit & 0xffff))
           | ((base & 0xffffff) << 16)
           | (0x89ull << 40)
           | (((uint64_t)(limit >> 16) & 0xf) << 48)
           | (((base >> 24) & 0xff) << 56);
    gdt[6] = base >> 32;
}

void gdt_init(void) {
    tss.rsp0 = gdt_kernel_stack_top();
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor();

    struct gdt_ptr gdt_ptr = {
        .limit = sizeof(gdt) - 1,
        .base = (uint64_t)gdt,
    };

    x86_64_load_gdt(&gdt_ptr);
    x86_64_load_tss(GDT_TSS);
}

uint64_t gdt_kernel_stack_top(void) {
    return (uint64_t)(kernel_stack + sizeof(kernel_stack));
}
