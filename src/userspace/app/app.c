#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "userspace/abi.h"
#include <stddef.h>
#include <stdint.h>

#define LINE_SIZE 128
#define HISTORY_SIZE 8
#define LOG_DUMP_MAX 4096
#define FS_LIST_MAX 32
#define FS_READ_CHUNK 1024

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
static char log_dump[LOG_DUMP_MAX];
static struct user_fs_dirent fs_entries[FS_LIST_MAX];
static char fs_read_buffer[FS_READ_CHUNK];
static char cwd_buffer[USER_FS_PATH_SIZE];
static char run_path_buffer[USER_FS_PATH_SIZE];

static void println(const char *s) {
    fputs(s);
    putchar('\n');
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

static int parse_decimal_size(const char *s, size_t *out) {
    if (*s == '\0') {
        return 0;
    }

    size_t value = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return 0;
        }

        size_t next = value * 10 + (size_t)(*s - '0');
        if (next < value) {
            return 0;
        }
        value = next;
    }

    *out = value;
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

    fputs(&buffer[pos]);
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

    fputs("0x");
    fputs(&buffer[pos]);
}

static void prompt(void) {
    fputs("u> ");
}

static void clear_line(void) {
    while (line_len > 0) {
        fputs("\b \b");
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

static struct user_input_event read_event(void) {
    struct user_input_event event;
    while (user_read_event(&event) == USER_ERR_AGAIN) {
    }
    return event;
}

static void command_mem(void);
static void print_bool(uint8_t value);

static void command_help(void) {
    println("commands:");
    println("  help      show this command list");
    println("  about     print userspace status");
    println("  userspace print scheduler and address-space stats");
    println("  apps      list filesystem userspace programs");
    println("  run       run a filesystem userspace program");
    println("  mem       print physical memory stats");
    println("  kheap     print kernel heap stats");
    println("  heap      test userspace malloc/sbrk");
    println("  fb        print framebuffer info");
    println("  log       print recent kernel log output");
    println("  ticks     print timer state");
    println("  fs        print filesystem status");
    println("  pwd       print current directory");
    println("  cd        change current directory");
    println("  ls        list ext2 directory contents");
    println("  cat       print an ext2 file");
    println("  touch     create an ext2 file");
    println("  mkdir     create an ext2 directory");
    println("  rm        remove an ext2 file");
    println("  rmdir     remove an empty ext2 directory");
    println("  truncate  resize an ext2 file");
    println("  edit      write text to an ext2 file");
    println("  echo      print command arguments");
    println("  color     set framebuffer text color");
    println("  clear     clear the terminal");
    println("  exit      return to the kernel shell");
}

static void command_about(void) {
    struct user_timer_status timer;
    println("userspace shell");
    println("  running in ring 3");
    println("  input: keyboard event syscall");
    println("  output: console putchar syscall");
    println("  editing: backspace, ctrl-l, history up/down");
    if (user_timer_status(&timer) == 0) {
        fputs("timer: ticks=");
        print_u64_dec(timer.ticks);
        fputs(" frequency=");
        print_u64_dec(timer.frequency_hz);
        println(" Hz");
    }
    command_mem();
}

static void command_mem(void) {
    struct user_memory_status memory;
    if (user_memory_status(&memory) != 0) {
        println("memory: syscall failed");
        return;
    }

    fputs("memory: total=");
    print_u64_dec(memory.total_kib);
    fputs(" KiB free=");
    print_u64_dec(memory.free_kib);
    fputs(" KiB used=");
    print_u64_dec(memory.used_kib);
    fputs(" KiB hhdm=");
    print_u64_hex(memory.hhdm_offset);
    putchar('\n');
}

static void command_kheap(void) {
    struct user_kernel_heap_status heap;
    if (user_kernel_heap_status(&heap) != 0) {
        println("kheap: syscall failed");
        return;
    }

    fputs("kheap: mapped=");
    print_u64_dec(heap.mapped_bytes);
    fputs(" allocated=");
    print_u64_dec(heap.allocated_bytes);
    fputs(" free=");
    print_u64_dec(heap.free_bytes);
    fputs(" valid=");
    print_bool(heap.valid);
    putchar('\n');
}

static void command_ticks(void) {
    struct user_timer_status timer;
    if (user_timer_status(&timer) != 0) {
        println("ticks: syscall failed");
        return;
    }

    fputs("ticks: ");
    print_u64_dec(timer.ticks);
    fputs(" frequency=");
    print_u64_dec(timer.frequency_hz);
    println(" Hz");
}

static void command_framebuffer(void) {
    struct user_framebuffer_status framebuffer;
    if (user_framebuffer_status(&framebuffer) != 0 || !framebuffer.available) {
        println("framebuffer: unavailable");
        return;
    }

    fputs("framebuffer: ");
    print_u64_dec(framebuffer.width);
    fputs("x");
    print_u64_dec(framebuffer.height);
    fputs(" pitch=");
    print_u64_dec(framebuffer.pitch);
    fputs(" bpp=");
    print_u64_dec(framebuffer.bpp);
    fputs(" addr=");
    print_u64_hex(framebuffer.address);
    putchar('\n');
}

static void print_bool(uint8_t value) {
    fputs(value ? "yes" : "no");
}

static int read_userspace_status(struct user_userspace_status *status) {
    if (user_userspace_status(status) != 0) {
        println("userspace: status syscall failed");
        return 0;
    }

    if (status->abi_version != USERSPACE_STATUS_ABI_VERSION) {
        println("userspace: unsupported status abi");
        return 0;
    }

    return 1;
}

static void command_userspace(void) {
    struct user_userspace_status status;
    if (!read_userspace_status(&status)) {
        return;
    }

    fputs("userspace: initialized=");
    print_bool(status.initialized);
    fputs(" syscall-dispatch=");
    print_bool(status.syscall_dispatch_ready);
    putchar('\n');

    fputs("userspace: scheduler=");
    print_bool(status.scheduler_ready);
    fputs(" preemptive=");
    print_bool(status.preemptive_enabled);
    fputs(" active=");
    print_bool(status.user_task_active);
    putchar('\n');

    fputs("userspace: launches=");
    print_u64_dec(status.launches);
    fputs(" exits=");
    print_u64_dec(status.exits);
    fputs(" ticks=");
    print_u64_dec(status.scheduler_ticks);
    fputs(" apps=");
    print_u64_dec(status.app_count);
    putchar('\n');

    fputs("userspace: heap=");
    print_u64_hex(status.heap_start);
    fputs("-");
    print_u64_hex(status.heap_current);
    fputs(" limit=");
    print_u64_hex(status.heap_end);
    putchar('\n');
}

static void command_apps(void) {
    long count = user_fs_list("/bin", fs_entries, FS_LIST_MAX);
    if (count < 0) {
        println("apps: cannot list /bin");
        return;
    }

    fputs("apps: ");
    print_u64_dec((uint64_t)count);
    putchar('\n');

    for (long i = 0; i < count; i++) {
        fputs("  ");
        fputs(fs_entries[i].name);
        fputs(" size=");
        print_u64_dec(fs_entries[i].size);
        putchar('\n');
    }
}

static void command_run(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: run <app-name|path>");
        return;
    }

    char *end = args;
    while (*end != '\0' && !is_space(*end)) {
        end++;
    }
    *end = '\0';

    const char *path = args;
    if (strchr(args, '/') == NULL) {
        size_t name_len = strlen(args);
        if (name_len + 6 > sizeof(run_path_buffer)) {
            println("run: name too long");
            return;
        }
        strcpy(run_path_buffer, "/bin/");
        strcpy(run_path_buffer + 5, args);
        path = run_path_buffer;
    }

    fputs("running ");
    fputs(args);
    println("...");

    long status = user_app_run_path(path);
    if (status < 0) {
        fputs("run: syscall failed ");
        print_u64_dec((uint64_t)(-status));
        putchar('\n');
        return;
    }

    fputs("run: exited with status ");
    print_u64_dec((uint64_t)status);
    putchar('\n');
}

static void command_heap(void) {
    struct user_userspace_status before;
    struct user_userspace_status after;
    if (!read_userspace_status(&before)) {
        return;
    }

    char *buffer = malloc(32);
    if (buffer == NULL) {
        println("heap: malloc failed");
        return;
    }

    strcpy(buffer, "heap allocation ok");
    println(buffer);

    if (!read_userspace_status(&after)) {
        return;
    }

    fputs("heap: before=");
    print_u64_hex(before.heap_current);
    fputs(" after=");
    print_u64_hex(after.heap_current);
    putchar('\n');
    free(buffer);
}

static void command_log(char *args) {
    struct user_klog_status status;
    if (user_klog_status(&status) != 0) {
        println("log: status syscall failed");
        return;
    }

    args = skip_spaces(args);
    size_t requested = 2048;
    if (*args != '\0') {
        if (strcmp(args, "all") == 0) {
            requested = LOG_DUMP_MAX;
        } else if (!parse_decimal_size(args, &requested)) {
            println("usage: log [bytes|all]");
            return;
        }
    }

    if (requested > LOG_DUMP_MAX) {
        requested = LOG_DUMP_MAX;
    }

    long copied = user_klog_read(log_dump, requested);
    if (copied < 0) {
        println("log: read syscall failed");
        return;
    }

    fputs("klog: showing ");
    print_u64_dec((uint64_t)copied);
    fputs("/");
    print_u64_dec(status.size);
    fputs(" bytes, dropped=");
    print_u64_dec(status.dropped);
    fputs(" total=");
    print_u64_dec(status.total_written);
    putchar('\n');

    for (long i = 0; i < copied; i++) {
        putchar(log_dump[i]);
    }

    if (copied == 0 || log_dump[copied - 1] != '\n') {
        putchar('\n');
    }
}

static void command_fs(void) {
    struct user_fs_status status;
    if (user_fs_status(&status) != 0
     || status.abi_version != USER_FS_STATUS_ABI_VERSION) {
        println("fs: status syscall failed");
        return;
    }

    if (!status.mounted) {
        println("fs: no rootfs mounted");
        return;
    }

    fputs("fs: ext2 mounted");
    if (status.volume_name[0] != '\0') {
        fputs(" volume=");
        fputs(status.volume_name);
    }
    putchar('\n');

    fputs("fs: block-size=");
    print_u64_dec(status.block_size);
    fputs(" blocks=");
    print_u64_dec(status.blocks);
    fputs(" free=");
    print_u64_dec(status.free_blocks);
    putchar('\n');

    fputs("fs: inodes=");
    print_u64_dec(status.inodes);
    fputs(" free=");
    print_u64_dec(status.free_inodes);
    putchar('\n');
}

static void command_pwd(void) {
    if (user_fs_getcwd(cwd_buffer, sizeof(cwd_buffer)) != 0) {
        println("pwd: syscall failed");
        return;
    }
    println(cwd_buffer);
}

static void command_cd(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        args = "/";
    }
    if (user_fs_chdir(args) != 0) {
        fputs("cd: cannot enter ");
        println(args);
    }
}

static void command_ls(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        args = ".";
    }

    long count = user_fs_list(args, fs_entries, FS_LIST_MAX);
    if (count < 0) {
        fputs("ls: cannot read ");
        println(args);
        return;
    }

    for (long i = 0; i < count; i++) {
        char marker = fs_entries[i].type == USER_FS_TYPE_DIR ? '/' : ' ';
        putchar(marker);
        fputs(fs_entries[i].name);
        fputs("  ");
        print_u64_dec(fs_entries[i].size);
        putchar('\n');
    }
}

static void command_edit(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: edit <path> <text>");
        return;
    }

    char *path = args;
    while (*args != '\0' && !is_space(*args)) {
        args++;
    }
    if (*args == '\0') {
        println("usage: edit <path> <text>");
        return;
    }
    *args++ = '\0';
    args = skip_spaces(args);

    size_t len = strlen(args);
    if (user_fs_truncate(path, len) != 0) {
        fputs("edit: cannot resize ");
        println(path);
        return;
    }

    long written = user_fs_write(path, args, len, 0);
    if (written < 0 || (size_t)written != len) {
        fputs("edit: cannot write ");
        println(path);
        return;
    }

    println("edit: wrote file");
}

static void command_touch(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: touch <path>");
        return;
    }
    if (user_fs_create(args) != 0) {
        fputs("touch: cannot create ");
        println(args);
        return;
    }
    println("touch: created file");
}

static void command_mkdir(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: mkdir <path>");
        return;
    }
    if (user_fs_mkdir(args) != 0) {
        fputs("mkdir: cannot create ");
        println(args);
        return;
    }
    println("mkdir: created directory");
}

static void command_rm(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: rm <path>");
        return;
    }
    if (user_fs_unlink(args) != 0) {
        fputs("rm: cannot remove ");
        println(args);
        return;
    }
    println("rm: removed file");
}

static void command_rmdir(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: rmdir <path>");
        return;
    }
    if (user_fs_rmdir(args) != 0) {
        fputs("rmdir: cannot remove ");
        println(args);
        return;
    }
    println("rmdir: removed directory");
}

static void command_truncate(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: truncate <path> <size>");
        return;
    }

    char *path = args;
    while (*args != '\0' && !is_space(*args)) {
        args++;
    }
    if (*args == '\0') {
        println("usage: truncate <path> <size>");
        return;
    }
    *args++ = '\0';
    args = skip_spaces(args);

    size_t size;
    if (!parse_decimal_size(args, &size)) {
        println("usage: truncate <path> <size>");
        return;
    }

    if (user_fs_truncate(path, size) != 0) {
        fputs("truncate: cannot resize ");
        println(path);
        return;
    }
    println("truncate: resized file");
}

static void command_cat(char *args) {
    args = skip_spaces(args);
    if (*args == '\0') {
        println("usage: cat <path>");
        return;
    }

    uint64_t offset = 0;
    char last = '\0';
    for (;;) {
        long copied = user_fs_read(args, fs_read_buffer, sizeof(fs_read_buffer),
                                   offset);
        if (copied < 0) {
            fputs("cat: cannot read ");
            println(args);
            return;
        }
        if (copied == 0) {
            break;
        }

        for (long i = 0; i < copied; i++) {
            putchar(fs_read_buffer[i]);
            last = fs_read_buffer[i];
        }
        offset += (uint64_t)copied;
    }

    if (offset != 0 && last != '\n') {
        putchar('\n');
    }
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
    fputs("colors:");
    for (size_t i = 0; i < sizeof(named_colors) / sizeof(named_colors[0]); i++) {
        putchar(' ');
        fputs(named_colors[i].name);
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
        user_reset_colors();
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

    user_set_color(fg_red, fg_green, fg_blue);

    if (*args != '\0') {
        uint8_t bg_red;
        uint8_t bg_green;
        uint8_t bg_blue;
        if (!parse_color_token(&args, &bg_red, &bg_green, &bg_blue)) {
            println("invalid background color");
            command_colors();
            return;
        }
        user_set_background(bg_red, bg_green, bg_blue);
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
    if (strcmp(command, "kheap") == 0) {
        command_kheap();
        return 0;
    }
    if (strcmp(command, "userspace") == 0) {
        command_userspace();
        return 0;
    }
    if (strcmp(command, "apps") == 0) {
        command_apps();
        return 0;
    }
    if (strcmp(command, "run") == 0) {
        command_run(args);
        return 0;
    }
    if (strcmp(command, "heap") == 0) {
        command_heap();
        return 0;
    }
    if (strcmp(command, "fb") == 0) {
        command_framebuffer();
        return 0;
    }
    if (strcmp(command, "log") == 0) {
        command_log(args);
        return 0;
    }
    if (strcmp(command, "ticks") == 0) {
        command_ticks();
        return 0;
    }
    if (strcmp(command, "fs") == 0) {
        command_fs();
        return 0;
    }
    if (strcmp(command, "pwd") == 0) {
        command_pwd();
        return 0;
    }
    if (strcmp(command, "cd") == 0) {
        command_cd(args);
        return 0;
    }
    if (strcmp(command, "ls") == 0) {
        command_ls(args);
        return 0;
    }
    if (strcmp(command, "cat") == 0) {
        command_cat(args);
        return 0;
    }
    if (strcmp(command, "touch") == 0) {
        command_touch(args);
        return 0;
    }
    if (strcmp(command, "mkdir") == 0) {
        command_mkdir(args);
        return 0;
    }
    if (strcmp(command, "rm") == 0) {
        command_rm(args);
        return 0;
    }
    if (strcmp(command, "rmdir") == 0) {
        command_rmdir(args);
        return 0;
    }
    if (strcmp(command, "truncate") == 0) {
        command_truncate(args);
        return 0;
    }
    if (strcmp(command, "edit") == 0) {
        command_edit(args);
        return 0;
    }
    if (strcmp(command, "clear") == 0) {
        user_clear();
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

    fputs("unknown command: ");
    println(command);
    return 0;
}

static int handle_char(char ch) {
    switch (ch) {
        case '\b':
            if (line_len > 0) {
                line_len--;
                line[line_len] = '\0';
                fputs("\b \b");
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
            user_clear();
            prompt();
            if (line_len > 0) {
                fputs(line);
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

static int handle_event(struct user_input_event event) {
    if (event.ch != '\0') {
        return handle_char(event.ch);
    }

    switch (event.key) {
        case USER_KEYBOARD_KEY_UP:
            history_previous();
            break;
        case USER_KEYBOARD_KEY_DOWN:
            history_next();
            break;
        case USER_KEYBOARD_KEY_ESCAPE:
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
