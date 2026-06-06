#include "terminal.h"

#include "drivers/serial.h"
#include "framebuffer/framebuffer.h"

static bool framebuffer_ready = false;
static bool framebuffer_enabled = true;
static bool serial_enabled = true;

#define DEFAULT_FG_RED   0xff
#define DEFAULT_FG_GREEN 0xff
#define DEFAULT_FG_BLUE  0xff
#define DEFAULT_BG_RED   0x00
#define DEFAULT_BG_GREEN 0x00
#define DEFAULT_BG_BLUE  0x00

void terminal_init(void) {
    init_serial();
}

bool terminal_attach_framebuffer(struct limine_framebuffer *framebuffer) {
    framebuffer_ready = framebuffer_console_init(framebuffer);
    return framebuffer_ready;
}

void terminal_set_serial_enabled(bool enabled) {
    serial_enabled = enabled;
}

void terminal_set_framebuffer_enabled(bool enabled) {
    framebuffer_enabled = enabled;
}

void terminal_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    if (framebuffer_ready) {
        framebuffer_console_set_color(red, green, blue);
    }
}

void terminal_set_background(uint8_t red, uint8_t green, uint8_t blue) {
    if (framebuffer_ready) {
        framebuffer_console_set_background(red, green, blue);
    }
}

void terminal_set_default_colors(void) {
    terminal_set_color(DEFAULT_FG_RED, DEFAULT_FG_GREEN, DEFAULT_FG_BLUE);
    terminal_set_background(DEFAULT_BG_RED, DEFAULT_BG_GREEN, DEFAULT_BG_BLUE);
}

void terminal_clear(void) {
    if (serial_enabled) {
        write_serial_string("\033[2J\033[H");
    }

    if (framebuffer_ready && framebuffer_enabled) {
        framebuffer_console_clear();
    }
}

void terminal_putchar(char ch) {
    if (serial_enabled) {
        if (ch == '\n') {
            write_serial('\r');
        }
        write_serial(ch);
    }

    if (framebuffer_ready && framebuffer_enabled) {
        framebuffer_console_putchar(ch);
    }
}

void terminal_write(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        terminal_putchar(s[i]);
    }
}
