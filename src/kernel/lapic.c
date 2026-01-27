#include "kernel/lapic.h"
#include "kernel/cpu.h"
#include "kernel/memory.h"

#define MSR_APIC_BASE 0x1B
#define APIC_ENABLE   (1ull << 11)

#define LAPIC_REG_ID        0x020
#define LAPIC_REG_EOI       0x0B0
#define LAPIC_REG_SVR       0x0F0
#define LAPIC_REG_TPR       0x080
#define LAPIC_REG_TIMER     0x320
#define LAPIC_REG_TIMER_ICR 0x380
#define LAPIC_REG_TIMER_CCR 0x390
#define LAPIC_REG_TIMER_DCR 0x3E0

#define LAPIC_TIMER_VECTOR  0xF0

static volatile u32 *lapic_regs = 0;
static u32 lapic_tps = 0;

static inline void lapic_write(u32 reg, u32 val) {
    lapic_regs[reg / 4] = val;
}

static inline u32 lapic_read(u32 reg) {
    return lapic_regs[reg / 4];
}

static void lapic_map(void) {
    u64 base = rdmsr(MSR_APIC_BASE);
    base |= APIC_ENABLE;
    wrmsr(MSR_APIC_BASE, base);
    u64 phys = base & 0xFFFFF000u;
    lapic_regs = (volatile u32 *)phys_to_virt(phys);
}

u32 lapic_id(void) {
    if (!lapic_regs) return 0;
    return lapic_read(LAPIC_REG_ID) >> 24;
}

void lapic_eoi(void) {
    if (!lapic_regs) return;
    lapic_write(LAPIC_REG_EOI, 0);
}

u32 lapic_timer_ticks_per_sec(void) {
    return lapic_tps;
}

static void lapic_timer_calibrate(void) {
    if (lapic_tps != 0) return;
    lapic_write(LAPIC_REG_TIMER_DCR, 0x3);
    lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_REG_TIMER_ICR, 0xFFFFFFFFu);

    u64 start = ticks;
    while (ticks == start) asm volatile("pause");
    start = ticks;
    while (ticks - start < PIT_HZ) asm volatile("pause");

    u32 cur = lapic_read(LAPIC_REG_TIMER_CCR);
    lapic_tps = 0xFFFFFFFFu - cur;
    if (lapic_tps == 0) lapic_tps = 100000000u;
}

void lapic_timer_setup(u32 hz) {
    if (!lapic_regs || hz == 0) return;
    if (lapic_tps == 0) lapic_timer_calibrate();
    u32 initial = lapic_tps / hz;
    if (initial == 0) initial = 1;
    lapic_write(LAPIC_REG_TIMER_DCR, 0x3);
    lapic_write(LAPIC_REG_TIMER, LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_REG_TIMER_ICR, initial);
}

void lapic_init(void) {
    lapic_map();
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x100 | 0xFF);
    lapic_timer_calibrate();
}

void lapic_init_ap(void) {
    lapic_map();
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x100 | 0xFF);
}
