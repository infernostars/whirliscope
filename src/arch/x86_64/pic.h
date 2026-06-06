#ifndef WHIRLISCOPE_ARCH_X86_64_PIC_H
#define WHIRLISCOPE_ARCH_X86_64_PIC_H
#pragma once

#include <stdint.h>

#define PIC_REMAP_OFFSET 0x20

void pic_init(void);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_send_eoi(uint8_t irq);
uint8_t pic_master_mask(void);
uint8_t pic_slave_mask(void);
uint16_t pic_read_irr(void);

#endif // WHIRLISCOPE_ARCH_X86_64_PIC_H
