#include "timer.h"

#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/pic.h"
#include "userspace/scheduler.h"
#include <stdbool.h>
#include <stddef.h>

#define PIT_INPUT_HZ 1193182u
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_MODE_RATE_GENERATOR 0x36

static volatile uint64_t ticks = 0;
static uint32_t current_frequency = 0;

static void timer_irq(struct interrupt_frame *frame) {
    ticks++;
    scheduler_tick(frame);
}

void timer_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        frequency_hz = 100;
    }

    uint32_t divisor = PIT_INPUT_HZ / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    } else if (divisor > 0xffff) {
        divisor = 0xffff;
    }

    current_frequency = PIT_INPUT_HZ / divisor;
    ticks = 0;

    irq_register_handler(0, timer_irq);
    outb(PIT_COMMAND, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0, divisor & 0xff);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xff);
    pic_unmask_irq(0);
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint32_t timer_frequency(void) {
    return current_frequency;
}

bool timer_wait_ticks(uint64_t wait_ticks, uint64_t spin_limit) {
    uint64_t target = timer_ticks() + wait_ticks;
    for (uint64_t i = 0; timer_ticks() < target && i < spin_limit; i++) {
        asm volatile ("pause");
    }
    return timer_ticks() >= target;
}
