#include "ata.h"

#include "arch/x86_64/io.h"
#include "drivers/block.h"
#include "kernel/klog.h"
#include <libc/mem.h>
#include <libc/string.h>

#define ATA_SECTOR_SIZE 512u
#define ATA_MAX_DEVICES 4u
#define ATA_TIMEOUT 1000000u

#define ATA_REG_DATA 0u
#define ATA_REG_ERROR 1u
#define ATA_REG_SECCOUNT0 2u
#define ATA_REG_LBA0 3u
#define ATA_REG_LBA1 4u
#define ATA_REG_LBA2 5u
#define ATA_REG_HDDEVSEL 6u
#define ATA_REG_COMMAND 7u
#define ATA_REG_STATUS 7u

#define ATA_CMD_READ_SECTORS 0x20u
#define ATA_CMD_WRITE_SECTORS 0x30u
#define ATA_CMD_IDENTIFY 0xecu

#define ATA_SR_ERR 0x01u
#define ATA_SR_DRQ 0x08u
#define ATA_SR_DF 0x20u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_BSY 0x80u

struct ata_channel {
    uint16_t io_base;
    uint16_t control_base;
    const char *name;
};

struct ata_device {
    struct block_device block;
    const struct ata_channel *channel;
    uint8_t drive;
};

static const struct ata_channel channels[] = {
    {.io_base = 0x1f0, .control_base = 0x3f6, .name = "ata0"},
    {.io_base = 0x170, .control_base = 0x376, .name = "ata1"},
};

static struct ata_device devices[ATA_MAX_DEVICES];
static size_t device_count;

static uint8_t ata_status(const struct ata_channel *channel) {
    return inb(channel->io_base + ATA_REG_STATUS);
}

static void ata_delay(const struct ata_channel *channel) {
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
    inb(channel->control_base);
}

static bool ata_wait_not_busy(const struct ata_channel *channel) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        if ((ata_status(channel) & ATA_SR_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool ata_wait_drq(const struct ata_channel *channel) {
    for (uint32_t i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = ata_status(channel);
        if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
            return false;
        }
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

static void ata_select_drive(const struct ata_channel *channel, uint8_t drive) {
    outb(channel->io_base + ATA_REG_HDDEVSEL,
         (uint8_t)(0xa0u | ((drive & 1u) << 4)));
    ata_delay(channel);
}

static bool ata_select_lba28(const struct ata_channel *channel, uint8_t drive,
                             uint32_t lba, uint8_t sectors) {
    if (!ata_wait_not_busy(channel)) {
        return false;
    }

    outb(channel->io_base + ATA_REG_HDDEVSEL,
         (uint8_t)(0xe0u | ((drive & 1u) << 4) | ((lba >> 24) & 0x0fu)));
    outb(channel->io_base + ATA_REG_SECCOUNT0, sectors);
    outb(channel->io_base + ATA_REG_LBA0, (uint8_t)lba);
    outb(channel->io_base + ATA_REG_LBA1, (uint8_t)(lba >> 8));
    outb(channel->io_base + ATA_REG_LBA2, (uint8_t)(lba >> 16));
    return true;
}

static bool ata_read_one(struct ata_device *device, uint32_t lba,
                         void *buffer) {
    uint16_t *out = buffer;
    const struct ata_channel *channel = device->channel;
    if (!ata_select_lba28(channel, device->drive, lba, 1)) {
        return false;
    }

    outb(channel->io_base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    if (!ata_wait_drq(channel)) {
        return false;
    }

    for (size_t i = 0; i < ATA_SECTOR_SIZE / sizeof(uint16_t); i++) {
        out[i] = inw(channel->io_base + ATA_REG_DATA);
    }
    ata_delay(channel);
    return true;
}

static bool ata_write_one(struct ata_device *device, uint32_t lba,
                          const void *buffer) {
    const uint16_t *in = buffer;
    const struct ata_channel *channel = device->channel;
    if (!ata_select_lba28(channel, device->drive, lba, 1)) {
        return false;
    }

    outb(channel->io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    if (!ata_wait_drq(channel)) {
        return false;
    }

    for (size_t i = 0; i < ATA_SECTOR_SIZE / sizeof(uint16_t); i++) {
        outw(channel->io_base + ATA_REG_DATA, in[i]);
    }
    ata_delay(channel);
    return true;
}

static bool ata_block_read(struct block_device *block, uint64_t lba,
                           void *buffer, size_t sector_count) {
    struct ata_device *device = block->driver_data;
    uint8_t *out = buffer;
    if (device == NULL || lba + sector_count > 0x10000000ull) {
        return false;
    }

    for (size_t i = 0; i < sector_count; i++) {
        if (!ata_read_one(device, (uint32_t)(lba + i),
                          out + i * ATA_SECTOR_SIZE)) {
            klog_printf("ata: read failed device=%s lba=%llu\n",
                        block->name, (unsigned long long)(lba + i));
            return false;
        }
    }
    return true;
}

static bool ata_block_write(struct block_device *block, uint64_t lba,
                            const void *buffer, size_t sector_count) {
    struct ata_device *device = block->driver_data;
    const uint8_t *in = buffer;
    if (device == NULL || lba + sector_count > 0x10000000ull) {
        return false;
    }

    for (size_t i = 0; i < sector_count; i++) {
        if (!ata_write_one(device, (uint32_t)(lba + i),
                           in + i * ATA_SECTOR_SIZE)) {
            klog_printf("ata: write failed device=%s lba=%llu\n",
                        block->name, (unsigned long long)(lba + i));
            return false;
        }
    }
    return true;
}

static void ata_model_string(char *dest, size_t dest_size,
                             const uint16_t *identify) {
    if (dest_size == 0) {
        return;
    }

    size_t pos = 0;
    for (size_t word = 27; word <= 46 && pos + 1 < dest_size; word++) {
        char hi = (char)(identify[word] >> 8);
        char lo = (char)(identify[word] & 0xff);
        if (pos + 1 < dest_size) {
            dest[pos++] = hi;
        }
        if (pos + 1 < dest_size) {
            dest[pos++] = lo;
        }
    }

    while (pos > 0 && dest[pos - 1] == ' ') {
        pos--;
    }
    dest[pos] = '\0';
}

static bool ata_identify(const struct ata_channel *channel, uint8_t drive,
                         uint16_t *identify) {
    ata_select_drive(channel, drive);
    outb(channel->io_base + ATA_REG_SECCOUNT0, 0);
    outb(channel->io_base + ATA_REG_LBA0, 0);
    outb(channel->io_base + ATA_REG_LBA1, 0);
    outb(channel->io_base + ATA_REG_LBA2, 0);
    outb(channel->io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(channel);

    uint8_t status = ata_status(channel);
    if (status == 0) {
        return false;
    }

    if (!ata_wait_not_busy(channel)) {
        return false;
    }

    uint8_t lba1 = inb(channel->io_base + ATA_REG_LBA1);
    uint8_t lba2 = inb(channel->io_base + ATA_REG_LBA2);
    if (lba1 != 0 || lba2 != 0) {
        return false;
    }

    if (!ata_wait_drq(channel)) {
        return false;
    }

    for (size_t i = 0; i < 256; i++) {
        identify[i] = inw(channel->io_base + ATA_REG_DATA);
    }
    return true;
}

static void ata_probe_drive(const struct ata_channel *channel, uint8_t drive) {
    if (device_count >= ATA_MAX_DEVICES) {
        return;
    }

    uint16_t identify[256];
    memset(identify, 0, sizeof(identify));
    if (!ata_identify(channel, drive, identify)) {
        return;
    }

    uint32_t sectors28 = (uint32_t)identify[60]
        | ((uint32_t)identify[61] << 16);
    if (sectors28 == 0) {
        klog_printf("ata: %s %s has no LBA28 sector count\n",
                    channel->name, drive == 0 ? "master" : "slave");
        return;
    }

    struct ata_device *device = &devices[device_count];
    memset(device, 0, sizeof(*device));
    device->channel = channel;
    device->drive = drive;
    device->block.sector_size = ATA_SECTOR_SIZE;
    device->block.sector_count = sectors28;
    device->block.writable = true;
    device->block.driver_data = device;
    device->block.read = ata_block_read;
    device->block.write = ata_block_write;
    strncpy(device->block.name, channel->name,
            sizeof(device->block.name) - 1);
    device->block.name[sizeof(device->block.name) - 1] = '\0';
    const char *suffix = drive == 0 ? "-master" : "-slave";
    size_t used = strlen(device->block.name);
    size_t suffix_len = strlen(suffix);
    if (suffix_len >= sizeof(device->block.name) - used) {
        suffix_len = sizeof(device->block.name) - used - 1;
    }
    memcpy(device->block.name + used, suffix, suffix_len);
    device->block.name[used + suffix_len] = '\0';

    char model[48];
    ata_model_string(model, sizeof(model), identify);
    klog_printf("ata: found %s model=\"%s\" sectors=%u\n",
                device->block.name, model, sectors28);

    if (block_register(&device->block)) {
        device_count++;
    }
}

void ata_init(void) {
    device_count = 0;
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); i++) {
        outb(channels[i].control_base, 0x02);
        ata_probe_drive(&channels[i], 0);
        ata_probe_drive(&channels[i], 1);
    }
    klog_printf("ata: initialized devices=%u\n", (unsigned)device_count);
}

size_t ata_device_count(void) {
    return device_count;
}
