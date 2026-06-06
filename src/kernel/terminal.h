#ifndef WHIRLISCOPE_TERMINAL_H
#define WHIRLISCOPE_TERMINAL_H
#pragma once

#include "included/limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void terminal_init(void);
bool terminal_attach_framebuffer(struct limine_framebuffer *framebuffer);
void terminal_set_serial_enabled(bool enabled);
void terminal_set_framebuffer_enabled(bool enabled);
void terminal_set_color(uint8_t red, uint8_t green, uint8_t blue);
void terminal_set_background(uint8_t red, uint8_t green, uint8_t blue);
void terminal_set_default_colors(void);
void terminal_clear(void);
void terminal_putchar(char ch);
void terminal_write(const char *s, size_t n);

#endif // WHIRLISCOPE_TERMINAL_H
