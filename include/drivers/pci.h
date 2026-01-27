#ifndef PCI_H
#define PCI_H

#include "types.h"

typedef struct {
    u8 bus;
    u8 slot;
    u8 func;
    u16 vendor_id;
    u16 device_id;
    u8 class_id;
    u8 subclass;
    u8 prog_if;
    u8 header_type;
    u8 irq_line;
    u32 bar[6];
} pci_device_t;

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset);
u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 value);
void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 value);
void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 value);

int pci_find_device(u16 vendor, u16 device, pci_device_t *out);
void pci_enable_bus_master(const pci_device_t *dev);
u64 pci_get_bar(const pci_device_t *dev, int index, int *is_mmio);

#endif
