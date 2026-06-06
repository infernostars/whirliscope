#ifndef WHIRLISCOPE_KERNEL_SHELL_H
#define WHIRLISCOPE_KERNEL_SHELL_H
#pragma once

#include "included/limine.h"

void shell_set_framebuffer(struct limine_framebuffer *framebuffer);

__attribute__((noreturn))
void shell_run(void);

#endif // WHIRLISCOPE_KERNEL_SHELL_H
