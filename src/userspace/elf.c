#include "elf.h"

#include "userspace/address_space.h"
#include "memory/vmm.h"
#include <libc/mem.h>

#define EI_NIDENT 16
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 62

struct elf64_ehdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

static const struct elf64_phdr *program_header_at(const void *image,
                                                  const struct elf64_ehdr *ehdr,
                                                  uint16_t index) {
    const uint8_t *bytes = image;
    return (const struct elf64_phdr *)(const void *)
        (bytes + ehdr->e_phoff + (uint64_t)ehdr->e_phentsize * index);
}

static bool range_within_image(uint64_t offset, uint64_t size,
                               uint64_t image_size) {
    if (size == 0) {
        return true;
    }

    uint64_t end = offset + size - 1;
    return end >= offset && end < image_size;
}

bool elf64_validate_user_image(const void *image, size_t image_size,
                               struct elf64_image *out) {
    if (image == NULL || image_size < sizeof(struct elf64_ehdr)) {
        return false;
    }

    const struct elf64_ehdr *ehdr = image;
    if (ehdr->e_ident[0] != ELFMAG0
     || ehdr->e_ident[1] != ELFMAG1
     || ehdr->e_ident[2] != ELFMAG2
     || ehdr->e_ident[3] != ELFMAG3
     || ehdr->e_ident[4] != ELFCLASS64
     || ehdr->e_ident[5] != ELFDATA2LSB
     || ehdr->e_ident[6] != EV_CURRENT) {
        return false;
    }

    if ((ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
     || ehdr->e_machine != EM_X86_64
     || ehdr->e_version != EV_CURRENT
     || ehdr->e_ehsize != sizeof(struct elf64_ehdr)
     || ehdr->e_phentsize != sizeof(struct elf64_phdr)
     || ehdr->e_phnum == 0) {
        return false;
    }

    if (!userspace_range_is_valid(ehdr->e_entry, 1)) {
        return false;
    }

    uint64_t ph_size = (uint64_t)ehdr->e_phentsize * ehdr->e_phnum;
    if (!range_within_image(ehdr->e_phoff, ph_size, image_size)) {
        return false;
    }

    bool entry_in_load_segment = false;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        const struct elf64_phdr *phdr = program_header_at(image, ehdr, i);
        if (phdr->p_type != ELF64_PT_LOAD) {
            continue;
        }

        if (phdr->p_memsz == 0) {
            continue;
        }

        if (phdr->p_memsz < phdr->p_filesz
         || (phdr->p_align != 0 && phdr->p_align < VMM_PAGE_SIZE)
         || !range_within_image(phdr->p_offset, phdr->p_filesz, image_size)
         || !userspace_range_is_valid(phdr->p_vaddr, phdr->p_memsz)) {
            return false;
        }

        if (ehdr->e_entry >= phdr->p_vaddr
         && ehdr->e_entry < phdr->p_vaddr + phdr->p_memsz) {
            entry_in_load_segment = true;
        }
    }

    if (!entry_in_load_segment) {
        return false;
    }

    if (out != NULL) {
        out->entry = ehdr->e_entry;
        out->phoff = ehdr->e_phoff;
        out->phnum = ehdr->e_phnum;
        out->phentsize = ehdr->e_phentsize;
    }

    return true;
}
