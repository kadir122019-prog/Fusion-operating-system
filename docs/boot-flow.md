# Boot Flow

1. Limine loads the kernel (`kernel.elf`) and provides boot protocol responses.
2. `_start` in `src/kernel/main.c` is the entry point.
3. Early init:
   - Initialize serial logging
   - Read Limine requests (framebuffer, memmap, HHDM, kernel address)
   - Initialize graphics and memory map
4. Core init:
   - Heap setup
   - Input, filesystem, network (existing subsystems)
   - Interrupts + LAPIC
5. Scheduler/tasks start (existing subsystem)

This document is intentionally minimal and should be updated as boot changes.
