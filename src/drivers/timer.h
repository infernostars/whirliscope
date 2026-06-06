#ifndef WHIRLISCOPE_DRIVERS_TIMER_H
#define WHIRLISCOPE_DRIVERS_TIMER_H
#pragma once

#include <stdbool.h>
#include <stdint.h>

void timer_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
uint32_t timer_frequency(void);
bool timer_wait_ticks(uint64_t ticks, uint64_t spin_limit);

#endif // WHIRLISCOPE_DRIVERS_TIMER_H
