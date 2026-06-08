#include "boot_info.h"

#include <stddef.h>
#include <stdint.h>

static struct limine_framebuffer *boot_framebuffer;

void boot_info_set_framebuffer(struct limine_framebuffer *framebuffer) {
    boot_framebuffer = framebuffer;
}

void boot_info_get_framebuffer_status(struct user_framebuffer_status *status) {
    if (status == NULL) {
        return;
    }

    *status = (struct user_framebuffer_status) {0};
    if (boot_framebuffer == NULL) {
        return;
    }

    status->available = 1;
    status->width = boot_framebuffer->width;
    status->height = boot_framebuffer->height;
    status->pitch = boot_framebuffer->pitch;
    status->bpp = boot_framebuffer->bpp;
    status->address = (uint64_t)boot_framebuffer->address;
}
