#ifndef CPU_H
#define CPU_H

#include "types.h"

extern volatile u64 ticks;
extern volatile u64 uptime_seconds;

#define PIT_HZ 60

static inline u8 inb(u16 port) {
    u8 ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline u16 inw(u16 port) {
    u16 ret;
    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline u32 inl(u16 port) {
    u32 ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(u16 port, u8 val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(u16 port, u16 val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(u16 port, u32 val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

void pit_init(u32 frequency);
void timer_handler(void);
void cpu_get_vendor(char *vendor);
void cpu_get_features(u32 *features_edx, u32 *features_ecx);
void cpu_sleep_ticks(u64 sleep_ticks);
u64 rdmsr(u32 msr);
void wrmsr(u32 msr, u64 value);

void reboot(void);
void halt(void);

#endif
