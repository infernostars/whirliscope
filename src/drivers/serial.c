#include "serial.h"
#include "arch/x86_64/io.h"

int init_serial() {
   outb(SERIAL_COM1 + 1, 0x00);    // Disable all interrupts
   outb(SERIAL_COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(SERIAL_COM1 + 0, 0x01);    // Set divisor to 1 (lo byte) 115200 baud
   outb(SERIAL_COM1 + 1, 0x00);    //                  (hi byte)
   outb(SERIAL_COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(SERIAL_COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(SERIAL_COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
   outb(SERIAL_COM1 + 4, 0x1E);    // Set in loopback mode, test the serial chip
   outb(SERIAL_COM1 + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

   // Check if serial is faulty (i.e: not same byte as sent)
   if(inb(SERIAL_COM1 + 0) != 0xAE) {
      return 1;
   }

   // If serial is not faulty set it in normal operation mode
   // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
   outb(SERIAL_COM1 + 4, 0x0F);
   return 0;
}

int serial_received() {
   return inb(SERIAL_COM1 + 5) & 1;
}

uint8_t read_serial() {
   while (serial_received() == 0);

   return inb(SERIAL_COM1);
}

int is_transmit_empty() {
   return inb(SERIAL_COM1 + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);

   outb(SERIAL_COM1,a);
}

void write_serial_string(const char *str) {
    for (const char *p = str; *p != '\0'; p++) {
        while (!is_transmit_empty()) {
            io_wait();
        }
        write_serial(*p);
    }
}
