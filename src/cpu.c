#include "cpu.h"

u64 ticks = 0;
u64 uptime_seconds = 0;

void pit_init(u32 frequency) {
    u32 divisor = 1193180 / frequency;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

void timer_handler(void) {
    ticks++;
    if (ticks % 100 == 0) {
        uptime_seconds++;
    }
}

void cpu_get_vendor(char *vendor) {
    u32 eax, ebx, ecx, edx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    *(u32 *)(vendor + 0) = ebx;
    *(u32 *)(vendor + 4) = edx;
    *(u32 *)(vendor + 8) = ecx;
    vendor[12] = 0;
}

void cpu_get_features(u32 *features_edx, u32 *features_ecx) {
    u32 eax, ebx;
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(*features_ecx), "=d"(*features_edx) : "a"(1));
}

void reboot(void) {
    u8 temp;
    asm volatile("cli");
    do {
        temp = inb(0x64);
        if (temp & 0x01) inb(0x60);
    } while (temp & 0x02);
    outb(0x64, 0xFE);
    while (1) asm volatile("hlt");
}

void halt(void) {
    while (1) asm volatile("hlt");
}
