#include "stdio.h"
#include "syscall.h"
#include "userspace/abi.h"

static int read_userspace_status(struct user_userspace_status *status) {
    if (user_userspace_status(status) != 0) {
        printf("userspace: status syscall failed");
        return 0;
    }

    if (status->abi_version != USERSPACE_STATUS_ABI_VERSION) {
        printf("userspace: unsupported status abi");
        return 0;
    }

    return 1;
}

static void print_bool(uint8_t value) {
    fputs(value ? "yes" : "no");
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

long app_main(void) {
    puts("hello: userspace program launched");
    struct user_userspace_status status;
    if (!read_userspace_status(&status)) {
        return 1;
    }

    fputs("hello: initialized=");
    print_bool(status.initialized);
    fputs(" syscall-dispatch=");
    print_bool(status.syscall_dispatch_ready);
    putchar('\n');

    fputs("hello: scheduler=");
    print_bool(status.scheduler_ready);
    fputs(" preemptive=");
    print_bool(status.preemptive_enabled);
    fputs(" active=");
    print_bool(status.user_task_active);
    putchar('\n');

    fputs("hello: launches=");
    print_u64_dec(status.launches);
    fputs(" exits=");
    print_u64_dec(status.exits);
    fputs(" ticks=");
    print_u64_dec(status.scheduler_ticks);
    fputs(" apps=");
    print_u64_dec(status.app_count);
    putchar('\n');

    fputs("hello: heap=");
    print_u64_hex(status.heap_start);
    fputs("-");
    print_u64_hex(status.heap_current);
    fputs(" limit=");
    print_u64_hex(status.heap_end);
    putchar('\n');
    user_exit(0);
    return 0;
}
