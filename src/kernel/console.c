#include "console.h"

#include "kernel/klog.h"
#include "kernel/terminal.h"

void console_init(void) {
    terminal_init();
}

void console_attach_framebuffer(struct limine_framebuffer *framebuffer) {
    (void)terminal_attach_framebuffer(framebuffer);
}

void console_set_serial_enabled(bool enabled) {
    terminal_set_serial_enabled(enabled);
}

void console_set_framebuffer_enabled(bool enabled) {
    terminal_set_framebuffer_enabled(enabled);
}

void console_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    terminal_set_color(red, green, blue);
}

void console_set_background(uint8_t red, uint8_t green, uint8_t blue) {
    terminal_set_background(red, green, blue);
}

void console_set_default_colors(void) {
    terminal_set_default_colors();
}

void console_clear(void) {
    terminal_clear();
}

void console_putchar(char ch) {
    klog_putchar(ch);
    terminal_putchar(ch);
}

void console_write(const char *s, size_t n) {
    klog_write(s, n);
    terminal_write(s, n);
}

void console_write_string(const char *s) {
    while (*s != '\0') {
        console_putchar(*s++);
    }
}
