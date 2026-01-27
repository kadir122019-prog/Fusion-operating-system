#include "drivers/virtio_blk.h"
#include "drivers/pci.h"
#include "kernel/cpu.h"
#include "kernel/memory.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_BLK_DEVICE_ID 0x1001

#define VIRTIO_PCI_DEVICE_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES  0x04
#define VIRTIO_PCI_QUEUE_ADDRESS   0x08
#define VIRTIO_PCI_QUEUE_SIZE      0x0C
#define VIRTIO_PCI_QUEUE_SELECT    0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY    0x10
#define VIRTIO_PCI_STATUS          0x12
#define VIRTIO_PCI_ISR             0x13
#define VIRTIO_PCI_CONFIG          0x14

#define VIRTIO_STATUS_ACK          0x01
#define VIRTIO_STATUS_DRIVER       0x02
#define VIRTIO_STATUS_DRIVER_OK    0x04

#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_desc {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed));

struct virtq_avail {
    u16 flags;
    u16 idx;
    u16 ring[0];
} __attribute__((packed));

struct virtq_used_elem {
    u32 id;
    u32 len;
} __attribute__((packed));

struct virtq_used {
    u16 flags;
    u16 idx;
    struct virtq_used_elem ring[0];
} __attribute__((packed));

struct virtio_blk_req {
    u32 type;
    u32 reserved;
    u64 sector;
} __attribute__((packed));

typedef struct {
    u16 io_base;
    u16 queue_size;
    struct virtq_desc *desc;
    struct virtq_avail *avail;
    struct virtq_used *used;
    u64 queue_phys;
    u16 last_used;
    struct virtio_blk_req *req;
    u64 req_phys;
    u8 *status;
    u64 status_phys;
    u8 *bounce;
    u64 bounce_phys;
    u64 capacity;
    int ready;
} virtio_blk_t;

static virtio_blk_t g_blk;

static u32 io_read32(u16 base, u16 off) {
    return inl((u16)(base + off));
}

static void io_write32(u16 base, u16 off, u32 val) {
    outl((u16)(base + off), val);
}

static u16 io_read16(u16 base, u16 off) {
    return inw((u16)(base + off));
}

static void io_write16(u16 base, u16 off, u16 val) {
    outw((u16)(base + off), val);
}

static u8 io_read8(u16 base, u16 off) {
    return inb((u16)(base + off));
}

static void io_write8(u16 base, u16 off, u8 val) {
    outb((u16)(base + off), val);
}

static u64 virtio_blk_read_capacity(u16 io_base) {
    u32 lo = io_read32(io_base, VIRTIO_PCI_CONFIG + 0);
    u32 hi = io_read32(io_base, VIRTIO_PCI_CONFIG + 4);
    return ((u64)hi << 32) | lo;
}

static int virtio_blk_submit(int write, u64 lba, u32 count, void *buffer) {
    if (!g_blk.ready) return 0;
    if (count == 0) return 0;
    u32 bytes = count * 512u;
    if (bytes > 4096) return 0;

    g_blk.req->type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    g_blk.req->reserved = 0;
    g_blk.req->sector = lba;
    g_blk.status[0] = 0xFF;

    g_blk.desc[0].addr = g_blk.req_phys;
    g_blk.desc[0].len = sizeof(struct virtio_blk_req);
    g_blk.desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_blk.desc[0].next = 1;

    g_blk.desc[1].addr = g_blk.bounce_phys;
    g_blk.desc[1].len = bytes;
    g_blk.desc[1].flags = VIRTQ_DESC_F_NEXT | (write ? 0 : VIRTQ_DESC_F_WRITE);
    g_blk.desc[1].next = 2;

    g_blk.desc[2].addr = g_blk.status_phys;
    g_blk.desc[2].len = 1;
    g_blk.desc[2].flags = VIRTQ_DESC_F_WRITE;
    g_blk.desc[2].next = 0;

    if (write) {
        memcpy(g_blk.bounce, buffer, bytes);
    }

    u16 idx = g_blk.avail->idx;
    g_blk.avail->ring[idx % g_blk.queue_size] = 0;
    g_blk.avail->idx = idx + 1;

    io_write16(g_blk.io_base, VIRTIO_PCI_QUEUE_NOTIFY, 0);

    while (g_blk.used->idx == g_blk.last_used) {
        io_read8(g_blk.io_base, VIRTIO_PCI_ISR);
    }
    g_blk.last_used = g_blk.used->idx;

    if (g_blk.status[0] != 0) return 0;
    if (!write) {
        memcpy(buffer, g_blk.bounce, bytes);
    }
    return 1;
}

int virtio_blk_init(void) {
    memset(&g_blk, 0, sizeof(g_blk));
    pci_device_t dev;
    if (!pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_BLK_DEVICE_ID, &dev)) {
        return 0;
    }
    pci_enable_bus_master(&dev);

    int is_mmio = 0;
    u64 bar0 = pci_get_bar(&dev, 0, &is_mmio);
    if (is_mmio || bar0 == 0) return 0;
    g_blk.io_base = (u16)bar0;

    io_write8(g_blk.io_base, VIRTIO_PCI_STATUS, 0);
    io_write8(g_blk.io_base, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    io_write16(g_blk.io_base, VIRTIO_PCI_QUEUE_SELECT, 0);
    u16 qsz = io_read16(g_blk.io_base, VIRTIO_PCI_QUEUE_SIZE);
    if (qsz == 0) return 0;
    if (qsz > 8) qsz = 8;
    g_blk.queue_size = qsz;

    size_t desc_sz = sizeof(struct virtq_desc) * qsz;
    size_t avail_sz = sizeof(struct virtq_avail) + sizeof(u16) * qsz;
    size_t used_sz = sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * qsz;
    size_t avail_off = (desc_sz + 1) & ~1u;
    size_t used_off = (avail_off + avail_sz + 3) & ~3u;
    size_t total = used_off + used_sz;

    u64 queue_phys = 0;
    void *queue_mem = phys_alloc(total, 4096, &queue_phys);
    if (!queue_mem) return 0;
    memset(queue_mem, 0, total);

    g_blk.queue_phys = queue_phys;
    g_blk.desc = (struct virtq_desc *)queue_mem;
    g_blk.avail = (struct virtq_avail *)((u8 *)queue_mem + avail_off);
    g_blk.used = (struct virtq_used *)((u8 *)queue_mem + used_off);
    g_blk.last_used = 0;

    io_write32(g_blk.io_base, VIRTIO_PCI_QUEUE_ADDRESS, (u32)(queue_phys / 4096));

    g_blk.req = (struct virtio_blk_req *)phys_alloc(sizeof(struct virtio_blk_req), 16, &g_blk.req_phys);
    g_blk.status = (u8 *)phys_alloc(1, 1, &g_blk.status_phys);
    g_blk.bounce = (u8 *)phys_alloc(4096, 16, &g_blk.bounce_phys);
    if (!g_blk.req || !g_blk.status || !g_blk.bounce) return 0;

    g_blk.capacity = virtio_blk_read_capacity(g_blk.io_base);

    io_write8(g_blk.io_base, VIRTIO_PCI_STATUS,
              io_read8(g_blk.io_base, VIRTIO_PCI_STATUS) | VIRTIO_STATUS_DRIVER_OK);
    g_blk.ready = 1;
    return 1;
}

int virtio_blk_is_ready(void) {
    return g_blk.ready;
}

u64 virtio_blk_capacity(void) {
    return g_blk.capacity;
}

int virtio_blk_read(u64 lba, u32 count, void *buffer) {
    u8 *out = (u8 *)buffer;
    while (count) {
        u32 chunk = count;
        if (chunk > 8) chunk = 8;
        if (!virtio_blk_submit(0, lba, chunk, out)) return 0;
        lba += chunk;
        count -= chunk;
        out += chunk * 512u;
    }
    return 1;
}

int virtio_blk_write(u64 lba, u32 count, const void *buffer) {
    const u8 *in = (const u8 *)buffer;
    while (count) {
        u32 chunk = count;
        if (chunk > 8) chunk = 8;
        if (!virtio_blk_submit(1, lba, chunk, (void *)in)) return 0;
        lba += chunk;
        count -= chunk;
        in += chunk * 512u;
    }
    return 1;
}
