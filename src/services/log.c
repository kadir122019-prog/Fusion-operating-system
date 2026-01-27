#include "services/log.h"
#include "drivers/serial.h"
#include "kernel/cpu.h"

static const char *level_to_str(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_PANIC: return "PANIC";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "LOG";
    }
}

static void u64_to_dec(char *out, int max, u64 value) {
    if (max <= 1) return;
    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    char tmp[24];
    int pos = 0;
    while (value && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    int out_pos = 0;
    while (pos > 0 && out_pos < max - 1) {
        out[out_pos++] = tmp[--pos];
    }
    out[out_pos] = 0;
}

void log_init(void) {
    serial_init();
}

void log_write(log_level_t level, const char *msg) {
    serial_write_str("[");
    serial_write_str(level_to_str(level));
    serial_write_str("] ");
    if (msg) {
        serial_write_str(msg);
    }
    serial_write_str("\n");
}

void panic(const char *file, int line, const char *msg) {
    char buf[24];
    asm volatile("cli");
    serial_write_str("[PANIC] ");
    if (file) {
        serial_write_str(file);
        serial_write_str(":");
    }
    u64_to_dec(buf, (int)sizeof(buf), (u64)line);
    serial_write_str(buf);
    serial_write_str(": ");
    if (msg) {
        serial_write_str(msg);
    }
    serial_write_str("\n");
    while (1) {
        asm volatile("hlt");
    }
}
