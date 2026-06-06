#ifndef WHIRLISCOPE_USERSPACE_ELF_H
#define WHIRLISCOPE_USERSPACE_ELF_H
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct elf64_image {
    uint64_t entry;
    uint64_t phoff;
    uint16_t phnum;
    uint16_t phentsize;
};

#define ELF64_PT_LOAD 1u

#define ELF64_PF_X 1u
#define ELF64_PF_W 2u
#define ELF64_PF_R 4u

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

bool elf64_validate_user_image(const void *image, size_t image_size,
                               struct elf64_image *out);

#endif // WHIRLISCOPE_USERSPACE_ELF_H
