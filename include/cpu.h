#ifndef CPU_H
#define CPU_H

#include "types.h"

extern u64 ticks;
extern u64 uptime_seconds;

static inline u8 inb(u16 port) {
    u8 ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(u16 port, u8 val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void pit_init(u32 frequency);
void timer_handler(void);
void cpu_get_vendor(char *vendor);
void cpu_get_features(u32 *features_edx, u32 *features_ecx);

void reboot(void);
void halt(void);

#endif
