#include "framebuffer.h"
#include "generated/font.h"

#include <stddef.h>
#include <stdint.h>

#define FONT_WIDTH WS_FONT_WIDTH
#define FONT_HEIGHT WS_FONT_HEIGHT
#define FONT_SCALE 2
#define CELL_WIDTH ((FONT_WIDTH + 1) * FONT_SCALE)
#define CELL_HEIGHT ((FONT_HEIGHT + 1) * FONT_SCALE)
#define TAB_WIDTH 4
#define MAX_CONSOLE_COLS 160
#define MAX_CONSOLE_ROWS 100

struct framebuffer_cell {
    char ch;
    uint32_t fg;
    uint32_t bg;
};

struct framebuffer_console {
    uint8_t *base;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint32_t fg;
    uint32_t bg;
    size_t cols;
    size_t rows;
    size_t cursor_col;
    size_t cursor_row;
    bool backing_ready;
    bool batch_active;
    bool redraw_deferred;
    bool ready;
};

static struct framebuffer_console console;
static struct framebuffer_cell cells[MAX_CONSOLE_ROWS][MAX_CONSOLE_COLS];

static const uint8_t (*const font)[FONT_HEIGHT] = ws_font;

static uint32_t color_component(uint8_t value, uint8_t mask_size,
                                uint8_t mask_shift) {
    if (mask_size == 0) {
        return 0;
    }

    uint32_t component = mask_size >= 8
        ? (uint32_t)value << (mask_size - 8)
        : (uint32_t)value >> (8 - mask_size);
    return component << mask_shift;
}

static uint32_t make_color(uint8_t red, uint8_t green, uint8_t blue) {
    return color_component(red, console.red_mask_size, console.red_mask_shift)
         | color_component(green, console.green_mask_size, console.green_mask_shift)
         | color_component(blue, console.blue_mask_size, console.blue_mask_shift);
}

static void clear_rect_color(size_t x, size_t y, size_t width, size_t height,
                             uint32_t color) {
    uint64_t wide_color = (uint64_t)color | ((uint64_t)color << 32);

    for (size_t row = 0; row < height; row++) {
        uint32_t *pixel = (uint32_t *)(console.base
            + (y + row) * console.pitch + x * 4);
        size_t col = 0;

        if (((uintptr_t)pixel & (sizeof(uint64_t) - 1)) == 0) {
            uint64_t *wide = (uint64_t *)pixel;
            for (; col + 1 < width; col += 2) {
                *wide++ = wide_color;
            }
            pixel = (uint32_t *)wide;
        }

        for (; col < width; col++) {
            *pixel++ = color;
        }
    }
}

static void clear_rect(size_t x, size_t y, size_t width, size_t height) {
    clear_rect_color(x, y, width, height, console.bg);
}

static void fill_span(size_t x, size_t y, size_t width, uint32_t color) {
    uint32_t *pixel = (uint32_t *)(console.base + y * console.pitch + x * 4);
    uint64_t wide_color = (uint64_t)color | ((uint64_t)color << 32);
    size_t i = 0;

    if (((uintptr_t)pixel & (sizeof(uint64_t) - 1)) == 0) {
        uint64_t *wide = (uint64_t *)pixel;
        for (; i + 1 < width; i += 2) {
            *wide++ = wide_color;
        }
        pixel = (uint32_t *)wide;
    }

    for (; i < width; i++) {
        *pixel++ = color;
    }
}

static void copy_row_forward(uint8_t *dest, const uint8_t *src, size_t bytes) {
    if ((((uintptr_t)dest | (uintptr_t)src) & (sizeof(uint64_t) - 1)) == 0) {
        while (bytes >= sizeof(uint64_t)) {
            *(uint64_t *)dest = *(const uint64_t *)src;
            dest += sizeof(uint64_t);
            src += sizeof(uint64_t);
            bytes -= sizeof(uint64_t);
        }
    }

    while (bytes > 0) {
        *dest++ = *src++;
        bytes--;
    }
}

static bool glyph_is_empty(const uint8_t *glyph) {
    for (size_t i = 0; i < FONT_HEIGHT; i++) {
        if (glyph[i] != 0) {
            return false;
        }
    }

    return true;
}

static const uint8_t *glyph_for(char ch) {
    unsigned char c = (unsigned char)ch;

    if (c < WS_FONT_FIRST || c > WS_FONT_LAST
     || glyph_is_empty(font[c - WS_FONT_FIRST])) {
        if (c == ' ') {
            return font[' ' - WS_FONT_FIRST];
        }
        return font['?' - WS_FONT_FIRST];
    }

    return font[c - WS_FONT_FIRST];
}

static void clear_cell_at(size_t col, size_t row) {
    clear_rect(col * CELL_WIDTH, row * CELL_HEIGHT, CELL_WIDTH, CELL_HEIGHT);
}

static void draw_cell_at(size_t col, size_t row, const struct framebuffer_cell *cell) {
    size_t x = col * CELL_WIDTH;
    size_t y = row * CELL_HEIGHT;
    const uint8_t *glyph = glyph_for(cell->ch);

    clear_rect_color(x, y, CELL_WIDTH, CELL_HEIGHT, cell->bg);
    if (cell->ch == ' ') {
        return;
    }

    for (size_t glyph_y = 0; glyph_y < FONT_HEIGHT; glyph_y++) {
        uint8_t row_bits = glyph[glyph_y];
        size_t pixel_y = y + 1 + glyph_y * FONT_SCALE;

        for (size_t glyph_x = 0; glyph_x < FONT_WIDTH;) {
            uint8_t bit = 1u << (FONT_WIDTH - 1 - glyph_x);
            if ((row_bits & bit) == 0) {
                glyph_x++;
                continue;
            }

            size_t run_start = glyph_x;
            do {
                glyph_x++;
                if (glyph_x >= FONT_WIDTH) {
                    break;
                }
                bit = 1u << (FONT_WIDTH - 1 - glyph_x);
            } while ((row_bits & bit) != 0);

            size_t pixel_x = x + 1 + run_start * FONT_SCALE;
            size_t run_width = (glyph_x - run_start) * FONT_SCALE;
            for (size_t dy = 0; dy < FONT_SCALE; dy++) {
                fill_span(pixel_x, pixel_y + dy, run_width, cell->fg);
            }
        }
    }
}

static void set_cell(size_t col, size_t row, char ch) {
    if (!console.backing_ready) {
        return;
    }

    cells[row][col] = (struct framebuffer_cell) {
        .ch = ch,
        .fg = console.fg,
        .bg = console.bg,
    };
}

static void clear_backing_row(size_t row) {
    if (!console.backing_ready) {
        return;
    }

    for (size_t col = 0; col < console.cols; col++) {
        cells[row][col] = (struct framebuffer_cell) {
            .ch = ' ',
            .fg = console.fg,
            .bg = console.bg,
        };
    }
}

static void clear_backing(void) {
    if (!console.backing_ready) {
        return;
    }

    for (size_t row = 0; row < console.rows; row++) {
        clear_backing_row(row);
    }
}

static void redraw_from_backing(void) {
    if (!console.backing_ready) {
        return;
    }

    for (size_t row = 0; row < console.rows; row++) {
        for (size_t col = 0; col < console.cols; col++) {
            draw_cell_at(col, row, &cells[row][col]);
        }
    }
}

static void draw_char_at(size_t col, size_t row, char ch) {
    struct framebuffer_cell cell = {
        .ch = ch,
        .fg = console.fg,
        .bg = console.bg,
    };

    if (console.backing_ready) {
        cells[row][col] = cell;
    }

    if (console.redraw_deferred) {
        return;
    }

    draw_cell_at(col, row, &cell);
}

static void scroll(void) {
    if (console.backing_ready) {
        for (size_t row = 1; row < console.rows; row++) {
            for (size_t col = 0; col < console.cols; col++) {
                cells[row - 1][col] = cells[row][col];
            }
        }

        clear_backing_row(console.rows - 1);
        if (console.batch_active) {
            console.redraw_deferred = true;
            return;
        }

        redraw_from_backing();
        return;
    }

    size_t copy_width = console.cols * CELL_WIDTH * 4;
    size_t copy_height = (console.rows - 1) * CELL_HEIGHT;

    for (size_t y = 0; y < copy_height; y++) {
        uint8_t *dest = console.base + y * console.pitch;
        uint8_t *src = console.base + (y + CELL_HEIGHT) * console.pitch;
        copy_row_forward(dest, src, copy_width);
    }

    clear_rect(0, (console.rows - 1) * CELL_HEIGHT,
               console.cols * CELL_WIDTH, CELL_HEIGHT);
}

static void flush_deferred_redraw(void) {
    if (!console.redraw_deferred) {
        return;
    }

    redraw_from_backing();
    console.redraw_deferred = false;
}

static void newline(void) {
    console.cursor_col = 0;
    console.cursor_row++;

    if (console.cursor_row >= console.rows) {
        scroll();
        console.cursor_row = console.rows - 1;
    }
}

bool framebuffer_console_init(struct limine_framebuffer *framebuffer) {
    if (framebuffer == NULL || framebuffer->address == NULL
     || framebuffer->bpp != 32) {
        console.ready = false;
        return false;
    }

    console.base = framebuffer->address;
    console.width = framebuffer->width;
    console.height = framebuffer->height;
    console.pitch = framebuffer->pitch;
    console.red_mask_size = framebuffer->red_mask_size;
    console.red_mask_shift = framebuffer->red_mask_shift;
    console.green_mask_size = framebuffer->green_mask_size;
    console.green_mask_shift = framebuffer->green_mask_shift;
    console.blue_mask_size = framebuffer->blue_mask_size;
    console.blue_mask_shift = framebuffer->blue_mask_shift;
    console.cols = console.width / CELL_WIDTH;
    console.rows = console.height / CELL_HEIGHT;
    console.cursor_col = 0;
    console.cursor_row = 0;
    console.backing_ready = console.cols <= MAX_CONSOLE_COLS
        && console.rows <= MAX_CONSOLE_ROWS;

    if (console.cols == 0 || console.rows == 0) {
        console.ready = false;
        return false;
    }

    console.fg = make_color(0xff, 0xff, 0xff);
    console.bg = make_color(0x00, 0x00, 0x00);
    console.ready = true;
    framebuffer_console_clear();
    return true;
}

void framebuffer_console_set_color(uint8_t red, uint8_t green, uint8_t blue) {
    if (!console.ready) {
        return;
    }

    console.fg = make_color(red, green, blue);
}

void framebuffer_console_set_background(uint8_t red, uint8_t green, uint8_t blue) {
    if (!console.ready) {
        return;
    }

    console.bg = make_color(red, green, blue);
}

static void putchar_inner(char ch) {
    if (!console.ready) {
        return;
    }

    switch (ch) {
        case '\n':
            newline();
            return;
        case '\r':
            console.cursor_col = 0;
            return;
        case '\t': {
            size_t next_tab = (console.cursor_col + TAB_WIDTH)
                & ~(size_t)(TAB_WIDTH - 1);
            while (console.cursor_col < next_tab) {
                framebuffer_console_putchar(' ');
            }
            return;
        }
        case '\b':
            if (console.cursor_col > 0) {
                console.cursor_col--;
                set_cell(console.cursor_col, console.cursor_row, ' ');
                clear_cell_at(console.cursor_col, console.cursor_row);
            }
            return;
        default:
            break;
    }

    if ((unsigned char)ch < ' ') {
        return;
    }

    if (console.cursor_col >= console.cols) {
        newline();
    }

    draw_char_at(console.cursor_col, console.cursor_row, ch);
    console.cursor_col++;

    if (console.cursor_col >= console.cols) {
        newline();
    }
}

void framebuffer_console_putchar(char ch) {
    putchar_inner(ch);
    flush_deferred_redraw();
}

void framebuffer_console_write(const char *s, size_t n) {
    if (!console.ready) {
        return;
    }

    bool was_batching = console.batch_active;
    console.batch_active = true;

    for (size_t i = 0; i < n; i++) {
        putchar_inner(s[i]);
    }

    console.batch_active = was_batching;
    if (!console.batch_active) {
        flush_deferred_redraw();
    }
}

void framebuffer_console_clear(void) {
    if (!console.ready) {
        return;
    }

    clear_rect(0, 0, console.width, console.height);
    clear_backing();
    console.redraw_deferred = false;
    console.cursor_col = 0;
    console.cursor_row = 0;
}
