#ifndef WHIRLISCOPE_DRIVERS_ATA_H
#define WHIRLISCOPE_DRIVERS_ATA_H
#pragma once

#include <stddef.h>

void ata_init(void);
size_t ata_device_count(void);

#endif // WHIRLISCOPE_DRIVERS_ATA_H
