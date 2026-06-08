#ifndef WHIRLISCOPE_KERNEL_BOOT_INFO_H
#define WHIRLISCOPE_KERNEL_BOOT_INFO_H
#pragma once

#include "included/limine.h"
#include "userspace/abi.h"

void boot_info_set_framebuffer(struct limine_framebuffer *framebuffer);
void boot_info_get_framebuffer_status(struct user_framebuffer_status *status);

#endif // WHIRLISCOPE_KERNEL_BOOT_INFO_H
