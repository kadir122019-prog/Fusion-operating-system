#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include "gfx.h"
#include "memory.h"
#include "cpu.h"
#include "interrupts.h"
#include "input.h"
#include "desktop.h"
#include "net.h"
#include "fs.h"

extern u8 __kernel_end[];

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

__attribute__((used, section(".requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER;

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
    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
        while (1) asm volatile("hlt");
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

    heap_init();
    gfx_enable_backbuffer(1);
    pmm_init();
    memory_set_memmap(memmap_request.response, kernel_phys_base, kernel_phys_end);
    input_init();
    interrupts_init();
    gfx_clear(0x000000);
    net_init();
    fs_init();

    desktop_init();
    desktop_loop();

    while (1) asm volatile("hlt");
}
