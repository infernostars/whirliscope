#include "block.h"

#include "kernel/klog.h"
#include <libc/mem.h>
#include <libc/string.h>

#define BLOCK_MAX_DEVICES 8u

static struct block_device *devices[BLOCK_MAX_DEVICES];
static size_t device_count;

void block_init(void) {
    memset(devices, 0, sizeof(devices));
    device_count = 0;
}

bool block_register(struct block_device *device) {
    if (device == NULL || device->sector_size == 0
     || device->sector_count == 0 || device->read == NULL
     || device_count >= BLOCK_MAX_DEVICES) {
        return false;
    }

    devices[device_count++] = device;
    klog_printf("block: registered %s sectors=%llu size=%u writable=%s\n",
                device->name,
                (unsigned long long)device->sector_count,
                device->sector_size,
                device->writable ? "yes" : "no");
    return true;
}

size_t block_device_count(void) {
    return device_count;
}

struct block_device *block_device_at(size_t index) {
    return index < device_count ? devices[index] : NULL;
}

struct block_device *block_first_writable(void) {
    for (size_t i = 0; i < device_count; i++) {
        if (devices[i] != NULL && devices[i]->writable) {
            return devices[i];
        }
    }
    return NULL;
}

bool block_read(struct block_device *device, uint64_t lba, void *buffer,
                size_t sector_count) {
    if (device == NULL || buffer == NULL || device->read == NULL
     || sector_count == 0 || lba > device->sector_count
     || sector_count > device->sector_count - lba) {
        return false;
    }

    return device->read(device, lba, buffer, sector_count);
}

bool block_write(struct block_device *device, uint64_t lba,
                 const void *buffer, size_t sector_count) {
    if (device == NULL || buffer == NULL || device->write == NULL
     || !device->writable || sector_count == 0 || lba > device->sector_count
     || sector_count > device->sector_count - lba) {
        return false;
    }

    return device->write(device, lba, buffer, sector_count);
}
