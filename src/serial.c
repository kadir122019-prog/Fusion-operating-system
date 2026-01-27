#include "serial.h"
#include "cpu.h"

#define COM1_PORT 0x3F8

static int serial_initialized = 0;

void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
    serial_initialized = 1;
}

static int serial_tx_ready(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

void serial_write_char(char c) {
    if (!serial_initialized) {
        serial_init();
    }
    while (!serial_tx_ready()) {
        asm volatile("pause");
    }
    outb(COM1_PORT, (u8)c);
}

void serial_write_str(const char *s) {
    if (!s) return;
    while (*s) {
        if (*s == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(*s++);
    }
}
