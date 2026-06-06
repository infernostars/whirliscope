#ifndef WHIRLISCOPE_CONSOLE_H
#define WHIRLISCOPE_CONSOLE_H
#pragma once

#include "included/limine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void console_init(void);
void console_attach_framebuffer(struct limine_framebuffer *framebuffer);
void console_set_serial_enabled(bool enabled);
void console_set_framebuffer_enabled(bool enabled);
void console_set_color(uint8_t red, uint8_t green, uint8_t blue);
void console_set_background(uint8_t red, uint8_t green, uint8_t blue);
void console_set_default_colors(void);
void console_clear(void);
void console_putchar(char ch);
void console_write(const char *s, size_t n);
void console_write_string(const char *s);

#endif // WHIRLISCOPE_CONSOLE_H
