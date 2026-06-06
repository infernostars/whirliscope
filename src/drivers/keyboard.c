#include "keyboard.h"

#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/pic.h"
#include <stddef.h>
#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_BUFFER_SIZE 128

static const char base_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0a] = '9', [0x0b] = '0', [0x0c] = '-', [0x0d] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']',
    [0x1e] = 'a', [0x1f] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2b] = '\\',
    [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c', [0x2f] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char shifted_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0a] = '(', [0x0b] = ')', [0x0c] = '_', [0x0d] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}',
    [0x1e] = 'A', [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2b] = '|',
    [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C', [0x2f] = 'V',
    [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static volatile size_t read_index = 0;
static volatile size_t write_index = 0;
static struct keyboard_event buffer[KEYBOARD_BUFFER_SIZE];
static bool shift_down = false;
static bool ctrl_down = false;
static bool alt_down = false;
static bool caps_lock = false;
static bool extended_scancode = false;

static bool is_letter(char ch) {
    return ch >= 'a' && ch <= 'z';
}

static uint8_t current_modifiers(void) {
    uint8_t modifiers = 0;
    if (shift_down) {
        modifiers |= KEYBOARD_MOD_SHIFT;
    }
    if (ctrl_down) {
        modifiers |= KEYBOARD_MOD_CTRL;
    }
    if (alt_down) {
        modifiers |= KEYBOARD_MOD_ALT;
    }
    if (caps_lock) {
        modifiers |= KEYBOARD_MOD_CAPS;
    }
    return modifiers;
}

static void push_event(enum keyboard_key key, char ch) {
    size_t next = (write_index + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == read_index) {
        return;
    }

    buffer[write_index] = (struct keyboard_event) {
        .key = key,
        .ch = ch,
        .modifiers = current_modifiers(),
    };
    write_index = next;
}

static enum keyboard_key extended_key_from_scancode(uint8_t scancode) {
    switch (scancode) {
        case 0x47:
            return KEYBOARD_KEY_HOME;
        case 0x48:
            return KEYBOARD_KEY_UP;
        case 0x4b:
            return KEYBOARD_KEY_LEFT;
        case 0x4d:
            return KEYBOARD_KEY_RIGHT;
        case 0x4f:
            return KEYBOARD_KEY_END;
        case 0x50:
            return KEYBOARD_KEY_DOWN;
        case 0x53:
            return KEYBOARD_KEY_DELETE;
        default:
            return KEYBOARD_KEY_NONE;
    }
}

static void keyboard_irq(struct interrupt_frame *frame) {
    (void)frame;

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    if (scancode == 0xe0) {
        extended_scancode = true;
        return;
    }

    if (extended_scancode) {
        extended_scancode = false;
        bool released = (scancode & 0x80) != 0;
        scancode &= 0x7f;

        if (scancode == 0x1d) {
            ctrl_down = !released;
            return;
        }
        if (scancode == 0x38) {
            alt_down = !released;
            return;
        }

        if (!released) {
            enum keyboard_key key = extended_key_from_scancode(scancode);
            if (key != KEYBOARD_KEY_NONE) {
                push_event(key, '\0');
            }
        }
        return;
    }

    bool released = (scancode & 0x80) != 0;
    scancode &= 0x7f;

    if (scancode == 0x2a || scancode == 0x36) {
        shift_down = !released;
        return;
    }

    if (scancode == 0x1d) {
        ctrl_down = !released;
        return;
    }

    if (scancode == 0x38) {
        alt_down = !released;
        return;
    }

    if (released) {
        return;
    }

    switch (scancode) {
        case 0x01:
            push_event(KEYBOARD_KEY_ESCAPE, 27);
            return;
        case 0x0e:
            push_event(KEYBOARD_KEY_BACKSPACE, '\b');
            return;
        case 0x0f:
            push_event(KEYBOARD_KEY_TAB, '\t');
            return;
        case 0x1c:
            push_event(KEYBOARD_KEY_ENTER, '\n');
            return;
        case 0x3a:
            caps_lock = !caps_lock;
            return;
        default:
            break;
    }

    char ch = shift_down ? shifted_map[scancode] : base_map[scancode];
    if (is_letter(base_map[scancode])) {
        bool uppercase = shift_down != caps_lock;
        ch = uppercase ? shifted_map[scancode] : base_map[scancode];
    }

    if (ctrl_down && is_letter(base_map[scancode])) {
        ch = base_map[scancode] - 'a' + 1;
    }

    if (ch != '\0') {
        push_event(KEYBOARD_KEY_CHAR, ch);
    }
}

void keyboard_init(void) {
    read_index = 0;
    write_index = 0;
    shift_down = false;
    ctrl_down = false;
    alt_down = false;
    caps_lock = false;
    extended_scancode = false;

    for (size_t i = 0; i < 256 && (inb(0x64) & 1) != 0; i++) {
        (void)inb(KEYBOARD_DATA_PORT);
    }

    irq_register_handler(1, keyboard_irq);
    pic_unmask_irq(1);
}

bool keyboard_read_event(struct keyboard_event *out) {
    if (read_index == write_index) {
        return false;
    }

    struct keyboard_event event = buffer[read_index];
    read_index = (read_index + 1) % KEYBOARD_BUFFER_SIZE;
    if (out != NULL) {
        *out = event;
    }
    return true;
}

bool keyboard_read_char(char *out) {
    struct keyboard_event event;
    while (keyboard_read_event(&event)) {
        if (event.ch != '\0') {
            if (out != NULL) {
                *out = event.ch;
            }
            return true;
        }
    }

    return false;
}
