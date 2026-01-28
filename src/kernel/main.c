#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "drivers/gfx.h"
#include "kernel/memory.h"
#include "kernel/cpu.h"
#include "kernel/interrupts.h"
#include "drivers/input.h"
#include "ui/desktop.h"
#include "services/net.h"
#include "services/fs.h"
#include "kernel/lapic.h"
#include "kernel/task.h"
#include "services/log.h"

extern u8 __kernel_end[];

#define BOOT_STACK_SIZE 16384
#define BOOT_STACK_CPUS 64

static u8 boot_stacks[BOOT_STACK_CPUS][BOOT_STACK_SIZE] __attribute__((aligned(16)));

static inline void boot_stack_set(u32 cpu_index) {
    if (cpu_index >= BOOT_STACK_CPUS) cpu_index = 0;
    u64 sp = (u64)boot_stacks[cpu_index] + BOOT_STACK_SIZE;
    sp &= ~0xFULL;
    asm volatile("mov %0, %%rsp" : : "r"(sp));
}

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

__attribute__((used, section(".requests")))
static volatile struct limine_smp_request mp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = 0
};

__attribute__((used, section(".requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void desktop_task(void *arg) {
    (void)arg;
    desktop_init();
    desktop_loop();
}

static void net_task(void *arg) {
    (void)arg;
    for (;;) {
        net_poll();
        task_sleep(1);
    }
}

static void ap_entry(struct limine_smp_info *info) {
    u32 index = (u32)info->extra_argument;
    boot_stack_set(index);
    asm volatile(
        "movw $0x30, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        :
        : "ax"
    );
    task_register_cpu(info->lapic_id, index);
    lapic_init_ap();
    interrupts_init_ap();
    task_start_ap();
    while (1) asm volatile("hlt");
}

void pmm_init(void) {
    if (memmap_request.response == NULL) return;

    struct limine_memmap_response *memmap = memmap_request.response;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t pages = entry->length / PAGE_SIZE;
        pmm_total_pages += pages;

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pmm_free_pages += pages;
        } else {
            pmm_used_pages += pages;
        }
    }
}

void _start(void) {
    log_init();
    LOG_INFO("kernel: booting");
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        PANIC("no framebuffer available");
    }

    u64 hhdm_offset = 0;
    if (hhdm_request.response != NULL) {
        hhdm_offset = hhdm_request.response->offset;
    }
    memory_set_hhdm_offset(hhdm_offset);

    u64 kernel_phys_base = 0;
    u64 kernel_phys_end = 0;
    if (kernel_address_request.response != NULL) {
        u64 virt_base = kernel_address_request.response->virtual_base;
        u64 phys_base = kernel_address_request.response->physical_base;
        u64 kernel_end = (u64)__kernel_end;
        kernel_phys_base = phys_base;
        if (kernel_end > virt_base) {
            kernel_phys_end = phys_base + (kernel_end - virt_base);
        }
    }

    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    uintptr_t fb_addr = (uintptr_t)framebuffer->address;
    if ((fb_addr & (1ull << 63)) == 0 && hhdm_offset != 0) {
        fb_addr += hhdm_offset;
    }
    gfx_init((u32 *)fb_addr, framebuffer->width, framebuffer->height, framebuffer->pitch);

    boot_stack_set(0);
    asm volatile(
        "movw $0x30, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        :
        : "ax"
    );

    heap_init();
    gfx_enable_backbuffer(1);
    pmm_init();
    memory_set_memmap(memmap_request.response, kernel_phys_base, kernel_phys_end);
    input_init();
    gfx_clear(0x000000);
    net_init();
    fs_init();

    u32 cpu_count = 1;
    if (mp_request.response && mp_request.response->cpu_count) {
        cpu_count = (u32)mp_request.response->cpu_count;
        for (u32 i = 0; i < cpu_count; i++) {
            struct limine_smp_info *cpu = mp_request.response->cpus[i];
            task_register_cpu(cpu->lapic_id, i);
        }
    }

    interrupts_init();
    lapic_init();
    interrupts_unmask_irq(12);

    task_init(cpu_count);
    task_create("desktop", desktop_task, NULL);
    task_create("net", net_task, NULL);

    if (mp_request.response) {
        for (u32 i = 0; i < cpu_count; i++) {
            struct limine_smp_info *cpu = mp_request.response->cpus[i];
            if (cpu->lapic_id == mp_request.response->bsp_lapic_id) continue;
            cpu->goto_address = ap_entry;
            cpu->extra_argument = i;
        }
    }

    task_start_bsp();
    while (1) asm volatile("hlt");
}
