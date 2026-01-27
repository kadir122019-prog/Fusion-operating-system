#include "drivers/pci.h"
#include "kernel/cpu.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static u32 pci_make_addr(u8 bus, u8 slot, u8 func, u8 offset) {
    return (1u << 31) |
           ((u32)bus << 16) |
           ((u32)slot << 11) |
           ((u32)func << 8) |
           (offset & 0xFC);
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 value = pci_read32(bus, slot, func, offset & ~3u);
    u32 shift = (offset & 2u) * 8u;
    return (u16)((value >> shift) & 0xFFFFu);
}

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 value = pci_read32(bus, slot, func, offset & ~3u);
    u32 shift = (offset & 3u) * 8u;
    return (u8)((value >> shift) & 0xFFu);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 value) {
    u32 current = pci_read32(bus, slot, func, offset & ~3u);
    u32 shift = (offset & 2u) * 8u;
    u32 mask = 0xFFFFu << shift;
    u32 next = (current & ~mask) | ((u32)value << shift);
    pci_write32(bus, slot, func, offset & ~3u, next);
}

void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 value) {
    u32 current = pci_read32(bus, slot, func, offset & ~3u);
    u32 shift = (offset & 3u) * 8u;
    u32 mask = 0xFFu << shift;
    u32 next = (current & ~mask) | ((u32)value << shift);
    pci_write32(bus, slot, func, offset & ~3u, next);
}

static void pci_fill_device(u8 bus, u8 slot, u8 func, pci_device_t *dev) {
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = pci_read16(bus, slot, func, 0x00);
    dev->device_id = pci_read16(bus, slot, func, 0x02);
    dev->prog_if = pci_read8(bus, slot, func, 0x09);
    dev->subclass = pci_read8(bus, slot, func, 0x0A);
    dev->class_id = pci_read8(bus, slot, func, 0x0B);
    dev->header_type = pci_read8(bus, slot, func, 0x0E);
    dev->irq_line = pci_read8(bus, slot, func, 0x3C);
    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read32(bus, slot, func, (u8)(0x10 + i * 4));
    }
}

int pci_find_device(u16 vendor, u16 device, pci_device_t *out) {
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            u16 v0 = pci_read16((u8)bus, slot, 0, 0x00);
            if (v0 == 0xFFFF) continue;
            u8 header = pci_read8((u8)bus, slot, 0, 0x0E);
            int func_count = (header & 0x80) ? 8 : 1;
            for (u8 func = 0; func < func_count; func++) {
                u16 v = pci_read16((u8)bus, slot, func, 0x00);
                if (v == 0xFFFF) continue;
                u16 d = pci_read16((u8)bus, slot, func, 0x02);
                if (v == vendor && d == device) {
                    pci_fill_device((u8)bus, slot, func, out);
                    return 1;
                }
            }
        }
    }
    return 0;
}

void pci_enable_bus_master(const pci_device_t *dev) {
    u16 cmd = pci_read16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= 0x0006;
    pci_write16(dev->bus, dev->slot, dev->func, 0x04, cmd);
}

u64 pci_get_bar(const pci_device_t *dev, int index, int *is_mmio) {
    if (!dev || index < 0 || index > 5) return 0;
    u32 bar = dev->bar[index];
    if (bar & 0x1) {
        if (is_mmio) *is_mmio = 0;
        return (u64)(bar & ~0x3u);
    }
    if (is_mmio) *is_mmio = 1;
    u32 type = (bar >> 1) & 0x3;
    u64 addr = (u64)(bar & ~0xFu);
    if (type == 0x2 && index < 5) {
        u64 high = dev->bar[index + 1];
        addr |= (high << 32);
    }
    return addr;
}
