#ifndef WHIRLISCOPE_DRIVERS_KEYBOARD_H
#define WHIRLISCOPE_DRIVERS_KEYBOARD_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum keyboard_key {
    KEYBOARD_KEY_NONE = 0,
    KEYBOARD_KEY_CHAR,
    KEYBOARD_KEY_ESCAPE,
    KEYBOARD_KEY_BACKSPACE,
    KEYBOARD_KEY_TAB,
    KEYBOARD_KEY_ENTER,
    KEYBOARD_KEY_UP,
    KEYBOARD_KEY_DOWN,
    KEYBOARD_KEY_LEFT,
    KEYBOARD_KEY_RIGHT,
    KEYBOARD_KEY_HOME,
    KEYBOARD_KEY_END,
    KEYBOARD_KEY_DELETE,
};

enum keyboard_modifiers {
    KEYBOARD_MOD_SHIFT = 1u << 0,
    KEYBOARD_MOD_CTRL  = 1u << 1,
    KEYBOARD_MOD_ALT   = 1u << 2,
    KEYBOARD_MOD_CAPS  = 1u << 3,
};

struct keyboard_event {
    enum keyboard_key key;
    char ch;
    uint8_t modifiers;
};

void keyboard_init(void);
bool keyboard_read_event(struct keyboard_event *out);
bool keyboard_read_char(char *out);

#endif // WHIRLISCOPE_DRIVERS_KEYBOARD_H
