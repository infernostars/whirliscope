#ifndef WHIRLISCOPE_FRAMEBUFFER_H
#define WHIRLISCOPE_FRAMEBUFFER_H
#pragma once

#include "included/limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool framebuffer_console_init(struct limine_framebuffer *framebuffer);
void framebuffer_console_set_color(uint8_t red, uint8_t green, uint8_t blue);
void framebuffer_console_set_background(uint8_t red, uint8_t green, uint8_t blue);
void framebuffer_console_putchar(char ch);
void framebuffer_console_write(const char *s, size_t n);
void framebuffer_console_clear(void);

#endif // WHIRLISCOPE_FRAMEBUFFER_H
