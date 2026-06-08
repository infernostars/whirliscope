#include "shell.h"

#include "arch/x86_64/arch.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "kernel/console.h"
#include "kernel/klog.h"
#include "kernel/panic.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "memory/heap.h"
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "userspace/userspace.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHELL_LINE_SIZE 128
#define SHELL_HISTORY_SIZE 8
#define SHELL_LOG_DUMP_MAX 4096

static char line[SHELL_LINE_SIZE];
static size_t line_len = 0;
static char history[SHELL_HISTORY_SIZE][SHELL_LINE_SIZE];
static size_t history_count = 0;
static size_t history_cursor = 0;
static struct limine_framebuffer *boot_framebuffer = NULL;

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

void shell_set_framebuffer(struct limine_framebuffer *framebuffer) {
    boot_framebuffer = framebuffer;
}

static void shell_prompt(void) {
    printf("k> ");
}

static bool is_space(char ch) {
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

static bool split_command(char *input, char **command, char **args) {
    input = skip_spaces(input);
    trim_right(input);

    if (*input == '\0') {
        return false;
    }

    *command = input;
    while (*input != '\0' && !is_space(*input)) {
        input++;
    }

    if (*input != '\0') {
        *input++ = '\0';
    }
    *args = skip_spaces(input);
    return true;
}

static void clear_line_buffer(void) {
    while (line_len > 0) {
        printf("\b \b");
        line_len--;
    }
    line[0] = '\0';
}

static void set_line(const char *s) {
    clear_line_buffer();
    while (line_len + 1 < SHELL_LINE_SIZE && s[line_len] != '\0') {
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

    if (history_count < SHELL_HISTORY_SIZE) {
        strcpy(history[history_count++], s);
    } else {
        for (size_t i = 1; i < SHELL_HISTORY_SIZE; i++) {
            strcpy(history[i - 1], history[i]);
        }
        strcpy(history[SHELL_HISTORY_SIZE - 1], s);
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

static void command_help(void) {
    printf("kernel commands:\n");
    printf("  help       show this command list\n");
    printf("  about      print kernel status summary\n");
    printf("  fb         print framebuffer info\n");
    printf("  heap       print heap stats and validation state\n");
    printf("  log        print recent kernel log output\n");
    printf("  pmmtest    allocate, translate, and free one physical page\n");
    printf("  selftest   run heap and translation self-tests\n");
    printf("  ticks      print PIT tick counter\n");
    printf("  userspace  get details about userspace\n");
    printf("  userrun    enter the embedded user init task\n");
    printf("  panic      intentionally trigger panic/assert path\n");
}

static void command_mem(void) {
    struct pmm_stats memory = pmm_get_stats();
    printf("memory: total=%llu KiB free=%llu KiB used=%llu KiB hhdm=%#llx\n",
           (unsigned long long)(memory.total_bytes / 1024),
           (unsigned long long)(memory.free_bytes / 1024),
           (unsigned long long)(memory.used_bytes / 1024),
           (unsigned long long)pmm_hhdm_offset());
}

static void command_heap(void) {
    struct heap_stats heap = kheap_get_stats();
    printf("heap: mapped=%llu allocated=%llu free=%llu valid=%s\n",
           (unsigned long long)heap.mapped_bytes,
           (unsigned long long)heap.allocated_bytes,
           (unsigned long long)heap.free_bytes,
           kheap_validate() ? "yes" : "no");
}

static void command_about(void) {
    console_set_color(0, 0xa5, 0xa3);
    printf("Whirliscope ");
    console_set_color(255, 255, 255);
    printf("kernel\n");
    printf("timer: ticks=%llu frequency=%u Hz\n",
           (unsigned long long)timer_ticks(),
           (unsigned int)timer_frequency());
    command_mem();
    command_heap();
}

static void command_framebuffer(void) {
    if (boot_framebuffer == NULL) {
        printf("framebuffer: unavailable\n");
        return;
    }

    printf("framebuffer: %ux%u pitch=%u bpp=%u addr=%p\n",
           (unsigned int)boot_framebuffer->width,
           (unsigned int)boot_framebuffer->height,
           (unsigned int)boot_framebuffer->pitch,
           (unsigned int)boot_framebuffer->bpp,
           boot_framebuffer->address);
}

static bool parse_decimal_byte(const char *s, uint8_t *out) {
    if (*s == '\0') {
        return false;
    }

    unsigned int value = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }

        value = value * 10 + (unsigned int)(*s - '0');
        if (value > 255) {
            return false;
        }
    }

    *out = (uint8_t)value;
    return true;
}

static bool parse_decimal_size(const char *s, size_t *out) {
    if (*s == '\0') {
        return false;
    }

    size_t value = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }

        size_t next = value * 10 + (size_t)(*s - '0');
        if (next < value) {
            return false;
        }
        value = next;
    }

    *out = value;
    return true;
}

static bool parse_color_token(char **args, uint8_t *red, uint8_t *green,
                              uint8_t *blue) {
    char *name = *args;
    if (*name == '\0') {
        return false;
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
            return true;
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
        return true;
    }

    return false;
}

static void command_colors(void) {
    printf("colors:");
    for (size_t i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++) {
        printf(" %s", named_colors[i].name);
    }
    printf("\n");
    printf("usage: color reset | color list | color <fg> [bg]\n");
    printf("       color <r> <g> <b> [bg-r bg-g bg-b]\n");
}

static void command_color(char *args) {
    args = skip_spaces(args);
    if (*args == '\0' || strcmp(args, "list") == 0) {
        command_colors();
        return;
    }

    if (strcmp(args, "reset") == 0) {
        console_set_default_colors();
        printf("framebuffer colors reset\n");
        return;
    }

    uint8_t fg_red;
    uint8_t fg_green;
    uint8_t fg_blue;
    if (!parse_color_token(&args, &fg_red, &fg_green, &fg_blue)) {
        printf("invalid color\n");
        command_colors();
        return;
    }

    console_set_color(fg_red, fg_green, fg_blue);

    if (*args != '\0') {
        uint8_t bg_red;
        uint8_t bg_green;
        uint8_t bg_blue;
        if (!parse_color_token(&args, &bg_red, &bg_green, &bg_blue)) {
            printf("invalid background color\n");
            command_colors();
            return;
        }
        console_set_background(bg_red, bg_green, bg_blue);
    }

    printf("framebuffer color updated\n");
}

static void command_pmmtest(void) {
    uint64_t page = pmm_alloc_page();
    if (page == 0) {
        printf("pmmtest: allocation failed\n");
        return;
    }

    void *virtual_address = pmm_phys_to_virt(page);
    uint64_t translated = 0;
    bool translated_ok = vmm_virt_to_phys((uint64_t)virtual_address, &translated);
    printf("pmmtest: phys=%#llx virt=%p translated=%#llx ok=%s\n",
           (unsigned long long)page,
           virtual_address,
           (unsigned long long)translated,
           translated_ok && translated == page ? "yes" : "no");
    pmm_free_page(page);
}

static void command_selftest(void) {
    uint8_t *heap_a = kmalloc(64);
    assert(heap_a != NULL);
    for (size_t i = 0; i < 64; i++) {
        heap_a[i] = 0xa5;
    }

    uint64_t heap_phys = 0;
    assert(vmm_virt_to_phys((uint64_t)heap_a, &heap_phys));

    uint8_t *heap_b = kcalloc(4, 16);
    assert(heap_b != NULL);
    for (size_t i = 0; i < 64; i++) {
        assert(heap_b[i] == 0);
        heap_b[i] = (uint8_t)i;
    }

    heap_b = krealloc(heap_b, 160);
    assert(heap_b != NULL);
    for (size_t i = 0; i < 64; i++) {
        assert(heap_b[i] == (uint8_t)i);
    }

    kfree(heap_a);
    kfree(heap_b);
    assert(kheap_validate());
    command_pmmtest();
    printf("selftest: ok\n");
}

static void command_log(char *args) {
    static char dump[SHELL_LOG_DUMP_MAX];

    args = skip_spaces(args);
    size_t requested = 2048;
    if (*args != '\0') {
        if (strcmp(args, "all") == 0) {
            requested = SHELL_LOG_DUMP_MAX;
        } else if (!parse_decimal_size(args, &requested)) {
            printf("usage: log [bytes|all]\n");
            return;
        }
    }

    if (requested > SHELL_LOG_DUMP_MAX) {
        requested = SHELL_LOG_DUMP_MAX;
    }

    size_t available = klog_size();
    uint64_t dropped = klog_dropped();
    size_t copied = klog_copy_tail(dump, requested);
    printf("klog: showing %zu/%zu bytes, dropped=%llu total=%llu\n",
           copied,
           available,
           (unsigned long long)dropped,
           (unsigned long long)klog_total_written());

    for (size_t i = 0; i < copied; i++) {
        putchar(dump[i]);
    }

    if (copied == 0 || dump[copied - 1] != '\n') {
        putchar('\n');
    }
}

static void command_userspace(void) {
    struct userspace_status status = userspace_get_status();

    printf("userspace: initialized=%s syscall-dispatch=%s\n",
           status.initialized ? "yes" : "no",
           status.syscall_dispatch_ready ? "yes" : "no");
    printf("userspace: scheduler=%s preemptive=%s active=%s\n",
           status.scheduler_ready ? "yes" : "no",
           status.preemptive_enabled ? "yes" : "no",
           status.user_task_active ? "yes" : "no");
    printf("userspace: init-process=%s entry=%#llx pml4=%#llx\n",
           status.init_process_ready ? "ready" : "missing",
           (unsigned long long)status.init_entry,
           (unsigned long long)status.init_pml4);
    printf("userspace: launches=%llu exits=%llu exit-status=%lld\n",
           (unsigned long long)status.launches,
           (unsigned long long)status.exits,
           status.last_exit_status);
    printf("userspace: scheduler-ticks=%llu user-preemptions=%llu\n",
           (unsigned long long)status.scheduler_ticks,
           (unsigned long long)status.user_preemptions);
    printf("userspace: range=%#llx-%#llx image-base=%#llx stack-top=%#llx\n",
           (unsigned long long)status.user_min,
           (unsigned long long)status.user_max,
           (unsigned long long)status.image_base,
           (unsigned long long)status.user_stack_top);
    printf("userspace: kernel-base=%#llx elf-loader=segment-load\n",
           (unsigned long long)status.kernel_base);
}

static void run_command(char *input) {
    char *command;
    char *args;

    char history_line[SHELL_LINE_SIZE];
    strcpy(history_line, input);

    if (!split_command(input, &command, &args)) {
        return;
    }

    push_history(history_line);

    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "about") == 0) {
        command_about();
    } else if (strcmp(command, "fb") == 0) {
        command_framebuffer();
    } else if (strcmp(command, "heap") == 0) {
        command_heap();
    } else if (strcmp(command, "log") == 0) {
        command_log(args);
    } else if (strcmp(command, "pmmtest") == 0) {
        command_pmmtest();
    } else if (strcmp(command, "selftest") == 0) {
        command_selftest();
    } else if (strcmp(command, "ticks") == 0) {
        printf("ticks: %llu\n", (unsigned long long)timer_ticks());
    } else if (strcmp(command, "userspace") == 0) {
        command_userspace();
    } else if (strcmp(command, "userrun") == 0) {
        printf("entering userspace init\n");
        userspace_launch_init();
        printf("returned from userspace init\n");
    } else if (strcmp(command, "panic") == 0) {
        panic("panic command requested");
    } else {
        printf("unknown command: %s\n", command);
    }
}

static void handle_char(char ch) {
    switch (ch) {
        case '\b':
            if (line_len > 0) {
                line_len--;
                line[line_len] = '\0';
                printf("\b \b");
            }
            return;
        case '\n': {
            putchar('\n');
            line[line_len] = '\0';
            char command_line[SHELL_LINE_SIZE];
            strcpy(command_line, line);
            line_len = 0;
            line[0] = '\0';
            run_command(command_line);
            shell_prompt();
            return;
        }
        case 3:
            printf("^C\n");
            line_len = 0;
            line[0] = '\0';
            shell_prompt();
            return;
        case 12:
            console_clear();
            shell_prompt();
            if (line_len > 0) {
                printf("%s", line);
            }
            return;
        default:
            break;
    }

    if ((unsigned char)ch < ' ') {
        return;
    }

    if (line_len + 1 >= SHELL_LINE_SIZE) {
        return;
    }

    line[line_len++] = ch;
    line[line_len] = '\0';
    putchar(ch);
}

static void handle_event(struct keyboard_event event) {
    if (event.ch != '\0') {
        handle_char(event.ch);
        return;
    }

    switch (event.key) {
        case KEYBOARD_KEY_UP:
            history_previous();
            break;
        case KEYBOARD_KEY_DOWN:
            history_next();
            break;
        case KEYBOARD_KEY_ESCAPE:
            clear_line_buffer();
            break;
        default:
            break;
    }
}

__attribute__((noreturn))
void shell_run(void) {
    printf("Kernel shell ready. Type 'help'.\n");
    shell_prompt();

    for (;;) {
        struct keyboard_event event;
        while (keyboard_read_event(&event)) {
            handle_event(event);
        }

        arch_wait_for_interrupt();
    }
}
