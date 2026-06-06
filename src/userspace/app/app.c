#include <stddef.h>
#include <stdint.h>

#define SYSCALL_PUTCHAR 1
#define SYSCALL_EXIT 2
#define SYSCALL_READ_EVENT 4
#define SYSCALL_CLEAR 5
#define SYSCALL_SET_COLOR 6
#define SYSCALL_SET_BACKGROUND 7
#define SYSCALL_RESET_COLORS 8
#define SYSCALL_MEMORY_STATUS 9

#define ERR_AGAIN (-11L)

#define LINE_SIZE 128
#define HISTORY_SIZE 8

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

struct input_event {
    enum keyboard_key key;
    char ch;
    uint8_t modifiers;
};

struct memory_status {
    uint64_t total_kib;
    uint64_t free_kib;
    uint64_t used_kib;
    uint64_t hhdm_offset;
};

struct named_color {
    const char *name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

static const struct named_color named_colors[] = {
    {"black",   0x00, 0x00, 0x00},
    {"white",   0xff, 0xff, 0xff},
    {"gray",    0x80, 0x80, 0x80},
    {"red",     0xff, 0x5c, 0x5c},
    {"green",   0x5c, 0xff, 0x5c},
    {"yellow",  0xff, 0xff, 0x5c},
    {"blue",    0x5c, 0x5c, 0xff},
    {"magenta", 0xff, 0x5c, 0xff},
    {"cyan",    0x5c, 0xff, 0xff},
    {"whirl",   0x00, 0x75, 0x73},
};

static char line[LINE_SIZE];
static size_t line_len;
static char history[HISTORY_SIZE][LINE_SIZE];
static size_t history_count;
static size_t history_cursor;

static long syscall1(long number, long arg0) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi)
                  : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
                    "memory");
    return rax;
}

static long syscall3(long number, long arg0, long arg1, long arg2) {
    register long rax asm("rax") = number;
    register long rdi asm("rdi") = arg0;
    register long rsi asm("rsi") = arg1;
    register long rdx asm("rdx") = arg2;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi), "d"(rdx)
                  : "rcx", "r8", "r9", "r10", "r11", "memory");
    return rax;
}

static long syscall0(long number) {
    register long rax asm("rax") = number;

    asm volatile ("int $0x80"
                  : "+a"(rax)
                  :
                  : "rcx", "rdx", "rdi", "rsi", "r8", "r9", "r10", "r11",
                    "memory");
    return rax;
}

static long syscall_memory_status(struct memory_status *status) {
    register long rax asm("rax") = SYSCALL_MEMORY_STATUS;
    register long rdi asm("rdi") = 0;
    register long rsi asm("rsi") = 0;
    register long rdx asm("rdx") = 0;
    register long r10 asm("r10") = 0;

    asm volatile ("int $0x80"
                  : "+a"(rax), "+D"(rdi), "+S"(rsi), "+d"(rdx), "+r"(r10)
                  :
                  : "rcx", "r8", "r9", "r11", "memory");

    status->total_kib = (uint64_t)rdi;
    status->free_kib = (uint64_t)rsi;
    status->used_kib = (uint64_t)rdx;
    status->hhdm_offset = (uint64_t)r10;
    return rax;
}

static void putchar(char ch) {
    syscall1(SYSCALL_PUTCHAR, (unsigned char)ch);
}

static void puts(const char *s) {
    while (*s != '\0') {
        putchar(*s++);
    }
}

static void println(const char *s) {
    puts(s);
    putchar('\n');
}

static size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static int strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void strcpy(char *dest, const char *src) {
    while ((*dest++ = *src++) != '\0') {
    }
}

static int is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static char *skip_spaces(char *s) {
    while (is_space(*s)) {
        s++;
    }
    return s;
}

static void trim_right(char *s) {
    size_t len = strlen(s);
    while (len > 0 && is_space(s[len - 1])) {
        s[--len] = '\0';
    }
}

static int split_command(char *input, char **command, char **args) {
    input = skip_spaces(input);
    trim_right(input);

    if (*input == '\0') {
        return 0;
    }

    *command = input;
    while (*input != '\0' && !is_space(*input)) {
        input++;
    }

    if (*input != '\0') {
        *input++ = '\0';
    }
    *args = skip_spaces(input);
    return 1;
}

static void print_u64_dec(uint64_t value) {
    char buffer[21];
    size_t pos = sizeof(buffer);
    buffer[--pos] = '\0';

    do {
        buffer[--pos] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);

    puts(&buffer[pos]);
}

static void print_u64_hex(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char buffer[17];
    size_t pos = sizeof(buffer);
    buffer[--pos] = '\0';

    do {
        buffer[--pos] = digits[value & 0xf];
        value >>= 4;
    } while (value != 0);

    puts("0x");
    puts(&buffer[pos]);
}

static void prompt(void) {
    puts("u> ");
}

static void clear_line(void) {
    while (line_len > 0) {
        puts("\b \b");
        line_len--;
    }
    line[0] = '\0';
}

static void set_line(const char *s) {
    clear_line();
    while (line_len + 1 < LINE_SIZE && s[line_len] != '\0') {
        line[line_len] = s[line_len];
        putchar(line[line_len]);
        line_len++;
    }
    line[line_len] = '\0';
}

static void push_history(const char *s) {
    if (s[0] == '\0') {
        return;
    }

    if (history_count > 0 && strcmp(history[history_count - 1], s) == 0) {
        history_cursor = history_count;
        return;
    }

    if (history_count < HISTORY_SIZE) {
        strcpy(history[history_count++], s);
    } else {
        for (size_t i = 1; i < HISTORY_SIZE; i++) {
            strcpy(history[i - 1], history[i]);
        }
        strcpy(history[HISTORY_SIZE - 1], s);
    }
    history_cursor = history_count;
}

static void history_previous(void) {
    if (history_count == 0) {
        return;
    }
    if (history_cursor > 0) {
        history_cursor--;
    }
    set_line(history[history_cursor]);
}

static void history_next(void) {
    if (history_count == 0 || history_cursor >= history_count) {
        return;
    }

    history_cursor++;
    if (history_cursor == history_count) {
        set_line("");
    } else {
        set_line(history[history_cursor]);
    }
}

static struct input_event decode_event(long packed) {
    return (struct input_event) {
        .key = (enum keyboard_key)(packed & 0xff),
        .ch = (char)((packed >> 8) & 0xff),
        .modifiers = (uint8_t)((packed >> 16) & 0xff),
    };
}

static struct input_event read_event(void) {
    long packed;
    do {
        packed = syscall0(SYSCALL_READ_EVENT);
    } while (packed == ERR_AGAIN);

    return decode_event(packed);
}

static void command_help(void) {
    println("commands:");
    println("  help    show this command list");
    println("  about   print userspace status");
    println("  mem     print physical memory stats");
    println("  echo    print command arguments");
    println("  color   set framebuffer text color");
    println("  clear   clear the terminal");
    println("  exit    return to the kernel shell");
}

static void command_about(void) {
    println("userspace shell");
    println("  running in ring 3");
    println("  input: keyboard event syscall");
    println("  output: console putchar syscall");
    println("  editing: backspace, ctrl-l, history up/down");
}

static void command_mem(void) {
    struct memory_status memory;
    if (syscall_memory_status(&memory) != 0) {
        println("memory: syscall failed");
        return;
    }

    puts("memory: total=");
    print_u64_dec(memory.total_kib);
    puts(" KiB free=");
    print_u64_dec(memory.free_kib);
    puts(" KiB used=");
    print_u64_dec(memory.used_kib);
    puts(" KiB hhdm=");
    print_u64_hex(memory.hhdm_offset);
    putchar('\n');
}

static int parse_decimal_byte(const char *s, uint8_t *out) {
    if (*s == '\0') {
        return 0;
    }

    unsigned int value = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }

        value = value * 10 + (unsigned int)(*s - '0');
        if (value > 255) {
            return 0;
        }
    }

    *out = (uint8_t)value;
    return 1;
}

static int parse_color_token(char **args, uint8_t *red, uint8_t *green,
                             uint8_t *blue) {
    char *name = *args;
    if (*name == '\0') {
        return 0;
    }

    while (**args != '\0' && !is_space(**args)) {
        (*args)++;
    }
    if (**args != '\0') {
        *(*args)++ = '\0';
    }
    *args = skip_spaces(*args);

    for (size_t i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++) {
        if (strcmp(name, named_colors[i].name) == 0) {
            *red = named_colors[i].red;
            *green = named_colors[i].green;
            *blue = named_colors[i].blue;
            return 1;
        }
    }

    uint8_t parsed_red;
    uint8_t parsed_green;
    uint8_t parsed_blue;
    char *green_token = *args;
    while (**args != '\0' && !is_space(**args)) {
        (*args)++;
    }
    if (**args != '\0') {
        *(*args)++ = '\0';
    }
    *args = skip_spaces(*args);

    char *blue_token = *args;
    while (**args != '\0' && !is_space(**args)) {
        (*args)++;
    }
    if (**args != '\0') {
        *(*args)++ = '\0';
    }
    *args = skip_spaces(*args);

    if (parse_decimal_byte(name, &parsed_red)
     && parse_decimal_byte(green_token, &parsed_green)
     && parse_decimal_byte(blue_token, &parsed_blue)) {
        *red = parsed_red;
        *green = parsed_green;
        *blue = parsed_blue;
        return 1;
    }

    return 0;
}

static void command_colors(void) {
    puts("colors:");
    for (size_t i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++) {
        putchar(' ');
        puts(named_colors[i].name);
    }
    putchar('\n');
    println("usage: color reset | color list | color <fg> [bg]");
    println("       color <r> <g> <b> [bg-r bg-g bg-b]");
}

static void command_color(char *args) {
    args = skip_spaces(args);
    if (*args == '\0' || strcmp(args, "list") == 0) {
        command_colors();
        return;
    }

    if (strcmp(args, "reset") == 0) {
        syscall0(SYSCALL_RESET_COLORS);
        println("framebuffer colors reset");
        return;
    }

    uint8_t fg_red;
    uint8_t fg_green;
    uint8_t fg_blue;
    if (!parse_color_token(&args, &fg_red, &fg_green, &fg_blue)) {
        println("invalid color");
        command_colors();
        return;
    }

    syscall3(SYSCALL_SET_COLOR, fg_red, fg_green, fg_blue);

    if (*args != '\0') {
        uint8_t bg_red;
        uint8_t bg_green;
        uint8_t bg_blue;
        if (!parse_color_token(&args, &bg_red, &bg_green, &bg_blue)) {
            println("invalid background color");
            command_colors();
            return;
        }
        syscall3(SYSCALL_SET_BACKGROUND, bg_red, bg_green, bg_blue);
    }

    println("framebuffer color updated");
}

static int run_command(char *input) {
    char *command;
    char *args;

    input = skip_spaces(input);
    trim_right(input);
    if (*input == '\0') {
        return 0;
    }

    push_history(input);

    if (!split_command(input, &command, &args)) {
        return 0;
    }

    if (strcmp(command, "help") == 0) {
        command_help();
        return 0;
    }
    if (strcmp(command, "about") == 0) {
        command_about();
        return 0;
    }
    if (strcmp(command, "mem") == 0) {
        command_mem();
        return 0;
    }
    if (strcmp(command, "clear") == 0) {
        syscall0(SYSCALL_CLEAR);
        return 0;
    }
    if (strcmp(command, "color") == 0) {
        command_color(args);
        return 0;
    }
    if (strcmp(command, "exit") == 0) {
        println("leaving userspace shell");
        return 1;
    }
    if (strcmp(command, "echo") == 0) {
        println(args);
        return 0;
    }

    puts("unknown command: ");
    println(command);
    return 0;
}

static int handle_char(char ch) {
    switch (ch) {
        case '\b':
            if (line_len > 0) {
                line_len--;
                line[line_len] = '\0';
                puts("\b \b");
            }
            return 0;
        case '\n': {
            putchar('\n');
            line[line_len] = '\0';
            char command_line[LINE_SIZE];
            strcpy(command_line, line);
            line_len = 0;
            line[0] = '\0';
            if (run_command(command_line) != 0) {
                return 1;
            }
            prompt();
            return 0;
        }
        case 3:
            println("^C");
            line_len = 0;
            line[0] = '\0';
            prompt();
            return 0;
        case 12:
            syscall0(SYSCALL_CLEAR);
            prompt();
            if (line_len > 0) {
                puts(line);
            }
            return 0;
        default:
            break;
    }

    if ((unsigned char)ch < ' ' || line_len + 1 >= LINE_SIZE) {
        return 0;
    }

    line[line_len++] = ch;
    line[line_len] = '\0';
    putchar(ch);
    return 0;
}

static int handle_event(struct input_event event) {
    if (event.ch != '\0') {
        return handle_char(event.ch);
    }

    switch (event.key) {
        case KEYBOARD_KEY_UP:
            history_previous();
            break;
        case KEYBOARD_KEY_DOWN:
            history_next();
            break;
        case KEYBOARD_KEY_ESCAPE:
            clear_line();
            break;
        default:
            break;
    }

    return 0;
}

long app_main(void) {
    println("userspace shell ready");
    prompt();

    for (;;) {
        if (handle_event(read_event()) != 0) {
            return 0;
        }
    }
}
