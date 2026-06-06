#ifndef WHIRLISCOPE_SERIAL_H
#define WHIRLISCOPE_SERIAL_H
#pragma once
#include <stdint.h>

#define SERIAL_COM1 0x3f8

int init_serial();
int serial_received();
uint8_t read_serial();
int is_transmit_empty();
void write_serial(char a);
void write_serial_string(const char *str);

#endif // WHIRLISCOPE_SERIAL_H
