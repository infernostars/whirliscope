#ifndef WHIRLISCOPE_DRIVERS_BLOCK_H
#define WHIRLISCOPE_DRIVERS_BLOCK_H
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLOCK_DEVICE_NAME_SIZE 32u

struct block_device;

typedef bool (*block_read_fn)(struct block_device *device, uint64_t lba,
                              void *buffer, size_t sector_count);
typedef bool (*block_write_fn)(struct block_device *device, uint64_t lba,
                               const void *buffer, size_t sector_count);

struct block_device {
    char name[BLOCK_DEVICE_NAME_SIZE];
    uint32_t sector_size;
    uint64_t sector_count;
    bool writable;
    void *driver_data;
    block_read_fn read;
    block_write_fn write;
};

void block_init(void);
bool block_register(struct block_device *device);
size_t block_device_count(void);
struct block_device *block_device_at(size_t index);
struct block_device *block_first_writable(void);
bool block_read(struct block_device *device, uint64_t lba, void *buffer,
                size_t sector_count);
bool block_write(struct block_device *device, uint64_t lba,
                 const void *buffer, size_t sector_count);

#endif // WHIRLISCOPE_DRIVERS_BLOCK_H
