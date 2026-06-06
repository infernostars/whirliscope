#include "pic.h"

#include "arch/x86_64/io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA    0xa1

#define PIC_EOI 0x20
#define PIC_READ_IRR 0x0a

#define ICW1_INIT  0x10
#define ICW1_ICW4  0x01
#define ICW4_8086  0x01
#define IA32_APIC_BASE_MSR 0x1b
#define IA32_APIC_BASE_ENABLE (1ull << 11)

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    asm volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                  "d"((uint32_t)(value >> 32)));
}

static void pic_disable_local_apic(void) {
    uint64_t apic_base = read_msr(IA32_APIC_BASE_MSR);
    if ((apic_base & IA32_APIC_BASE_ENABLE) != 0) {
        write_msr(IA32_APIC_BASE_MSR, apic_base & ~IA32_APIC_BASE_ENABLE);
    }
}

static void pic_select_legacy_wire_mode(void) {
    outb(0x22, 0x70);
    io_wait();
    outb(0x23, 0x00);
    io_wait();
}

void pic_init(void) {
    pic_disable_local_apic();
    pic_select_legacy_wire_mode();

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, PIC_REMAP_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC_REMAP_OFFSET + 8);
    io_wait();

    outb(PIC1_DATA, 4);
    io_wait();
    outb(PIC2_DATA, 2);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t line = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) | (1u << line));
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
    uint8_t line = irq < 8 ? irq : irq - 8;
    outb(port, inb(port) & (uint8_t)~(1u << line));

    if (irq >= 8) {
        outb(PIC1_DATA, inb(PIC1_DATA) & (uint8_t)~(1u << 2));
    }
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

uint8_t pic_master_mask(void) {
    return inb(PIC1_DATA);
}

uint8_t pic_slave_mask(void) {
    return inb(PIC2_DATA);
}

uint16_t pic_read_irr(void) {
    outb(PIC1_COMMAND, PIC_READ_IRR);
    outb(PIC2_COMMAND, PIC_READ_IRR);
    return ((uint16_t)inb(PIC2_COMMAND) << 8) | inb(PIC1_COMMAND);
}
