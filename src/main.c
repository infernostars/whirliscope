#include "arch/x86_64/arch.h"
#include "boot/limine_requests.h"
#include "drivers/ata.h"
#include "drivers/block.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "fs/ext2.h"
#include "fs/rootfs.h"
#include "kernel/boot_info.h"
#include "kernel/console.h"
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

void kmain(void) {
    console_init();

    // Spec says we can have cases where bootloaders go to us if the version spec is higher than it understands:
    // "If a bootloader does not yet support a requested base revision
    // (i.e. if the requested base revision is higher than the maximum base revision supported),
    // it may boot the executable using any arbitrary revision it supports"
    // Why.
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        panic("limine base revision is not supported");
    }

    // ensure we got a framebuffer
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        panic("no framebuffer was provided");
    }

    // fetch the first framebuffer
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    console_attach_framebuffer(framebuffer);
    boot_info_set_framebuffer(framebuffer);
    arch_init();

    if (memmap_request.response == NULL || hhdm_request.response == NULL) {
        panic("memory map or hhdm response was not provided");
    }
    pmm_init(memmap_request.response, hhdm_request.response);
    vmm_init();
    kheap_init();
    block_init();
    ata_init();
    rootfs_mount_from_block_devices();
    if (!ext2_mounted() && module_request.response != NULL) {
        for (uint64_t i = 0; i < module_request.response->module_count; i++) {
            struct limine_file *module = module_request.response->modules[i];
            if (module != NULL && module->address != NULL
             && module->string != NULL
             && strcmp(module->string, "rootfs") == 0) {
                ext2_mount(module->address, module->size);
                break;
            }
        }
    }
    userspace_init();

    timer_init(100);
    keyboard_init();
    arch_enable_interrupts();
    bool timer_ok = timer_wait_ticks(5, 5000000);
    assert(timer_ok);


    userspace_launch_init();

    panic("userspace init exited");
}
