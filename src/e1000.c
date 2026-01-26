#include "e1000.h"
#include "pci.h"
#include "interrupts.h"
#include "memory.h"

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

#define E1000_REG_CTRL  0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EERD  0x0014
#define E1000_REG_ICR   0x00C0
#define E1000_REG_ICS   0x00C8
#define E1000_REG_IMS   0x00D0
#define E1000_REG_IMC   0x00D8
#define E1000_REG_RCTL  0x0100
#define E1000_REG_TCTL  0x0400
#define E1000_REG_TIPG  0x0410
#define E1000_REG_RDBAL 0x2800
#define E1000_REG_RDBAH 0x2804
#define E1000_REG_RDLEN 0x2808
#define E1000_REG_RDH   0x2810
#define E1000_REG_RDT   0x2818
#define E1000_REG_TDBAL 0x3800
#define E1000_REG_TDBAH 0x3804
#define E1000_REG_TDLEN 0x3808
#define E1000_REG_TDH   0x3810
#define E1000_REG_TDT   0x3818
#define E1000_REG_RAL0  0x5400
#define E1000_REG_RAH0  0x5404

#define E1000_RCTL_EN   (1u << 1)
#define E1000_RCTL_SBP  (1u << 2)
#define E1000_RCTL_UPE  (1u << 3)
#define E1000_RCTL_MPE  (1u << 4)
#define E1000_RCTL_LPE  (1u << 5)
#define E1000_RCTL_BAM  (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)

#define E1000_TCTL_EN   (1u << 1)
#define E1000_TCTL_PSP  (1u << 3)

#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 32
#define RX_BUF_SIZE 2048
#define TX_BUF_SIZE 2048

struct rx_desc {
    u64 addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} __attribute__((packed));

struct tx_desc {
    u64 addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} __attribute__((packed));

typedef struct {
    pci_device_t pci;
    volatile u32 *regs;
    u64 regs_phys;
    u8 irq;
    u8 mac[6];
    struct rx_desc *rx_descs;
    struct tx_desc *tx_descs;
    u8 *rx_bufs[RX_DESC_COUNT];
    u8 *tx_bufs[TX_DESC_COUNT];
    u64 rx_descs_phys;
    u64 tx_descs_phys;
    u64 rx_buf_phys[RX_DESC_COUNT];
    u64 tx_buf_phys[TX_DESC_COUNT];
    u32 rx_index;
    u32 tx_index;
    volatile int irq_fired;
    e1000_rx_cb rx_cb;
} e1000_device_t;

static e1000_device_t g_dev;
static int g_ready = 0;

static inline void reg_write(u32 reg, u32 value) {
    g_dev.regs[reg / 4] = value;
}

static inline u32 reg_read(u32 reg) {
    return g_dev.regs[reg / 4];
}

static int e1000_read_eeprom(u32 addr, u16 *data) {
    reg_write(E1000_REG_EERD, (addr << 8) | 1);
    for (int i = 0; i < 1000; i++) {
        u32 val = reg_read(E1000_REG_EERD);
        if (val & (1u << 4)) {
            *data = (u16)(val >> 16);
            return 1;
        }
    }
    return 0;
}

static void e1000_read_mac(u8 mac[6]) {
    u16 w0 = 0;
    u16 w1 = 0;
    u16 w2 = 0;
    if (e1000_read_eeprom(0, &w0) &&
        e1000_read_eeprom(1, &w1) &&
        e1000_read_eeprom(2, &w2)) {
        mac[0] = (u8)(w0 & 0xFF);
        mac[1] = (u8)(w0 >> 8);
        mac[2] = (u8)(w1 & 0xFF);
        mac[3] = (u8)(w1 >> 8);
        mac[4] = (u8)(w2 & 0xFF);
        mac[5] = (u8)(w2 >> 8);
    }
}

static int e1000_init_rx(void) {
    size_t desc_bytes = RX_DESC_COUNT * sizeof(struct rx_desc);
    g_dev.rx_descs = (struct rx_desc *)phys_alloc(desc_bytes, 16, &g_dev.rx_descs_phys);
    if (!g_dev.rx_descs) return 0;
    memset(g_dev.rx_descs, 0, desc_bytes);

    for (u32 i = 0; i < RX_DESC_COUNT; i++) {
        u64 phys = 0;
        g_dev.rx_bufs[i] = (u8 *)phys_alloc(RX_BUF_SIZE, 16, &phys);
        if (!g_dev.rx_bufs[i]) return 0;
        g_dev.rx_buf_phys[i] = phys;
        g_dev.rx_descs[i].addr = phys;
        g_dev.rx_descs[i].status = 0;
    }

    reg_write(E1000_REG_RDBAL, (u32)(g_dev.rx_descs_phys & 0xFFFFFFFFu));
    reg_write(E1000_REG_RDBAH, (u32)(g_dev.rx_descs_phys >> 32));
    reg_write(E1000_REG_RDLEN, (u32)desc_bytes);
    reg_write(E1000_REG_RDH, 0);
    reg_write(E1000_REG_RDT, RX_DESC_COUNT - 1);
    g_dev.rx_index = 0;

    u32 rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SECRC;
    reg_write(E1000_REG_RCTL, rctl);
    return 1;
}

static int e1000_init_tx(void) {
    size_t desc_bytes = TX_DESC_COUNT * sizeof(struct tx_desc);
    g_dev.tx_descs = (struct tx_desc *)phys_alloc(desc_bytes, 16, &g_dev.tx_descs_phys);
    if (!g_dev.tx_descs) return 0;
    memset(g_dev.tx_descs, 0, desc_bytes);

    for (u32 i = 0; i < TX_DESC_COUNT; i++) {
        u64 phys = 0;
        g_dev.tx_bufs[i] = (u8 *)phys_alloc(TX_BUF_SIZE, 16, &phys);
        if (!g_dev.tx_bufs[i]) return 0;
        g_dev.tx_buf_phys[i] = phys;
        g_dev.tx_descs[i].addr = phys;
        g_dev.tx_descs[i].status = 0x1;
    }

    reg_write(E1000_REG_TDBAL, (u32)(g_dev.tx_descs_phys & 0xFFFFFFFFu));
    reg_write(E1000_REG_TDBAH, (u32)(g_dev.tx_descs_phys >> 32));
    reg_write(E1000_REG_TDLEN, (u32)desc_bytes);
    reg_write(E1000_REG_TDH, 0);
    reg_write(E1000_REG_TDT, 0);
    g_dev.tx_index = 0;

    u32 tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12);
    reg_write(E1000_REG_TCTL, tctl);
    reg_write(E1000_REG_TIPG, 0x0060200A);
    return 1;
}

static void e1000_configure_mac(void) {
    u32 ral = (u32)g_dev.mac[0] |
              ((u32)g_dev.mac[1] << 8) |
              ((u32)g_dev.mac[2] << 16) |
              ((u32)g_dev.mac[3] << 24);
    u32 rah = (u32)g_dev.mac[4] |
              ((u32)g_dev.mac[5] << 8) |
              (1u << 31);
    reg_write(E1000_REG_RAL0, ral);
    reg_write(E1000_REG_RAH0, rah);
}

static void e1000_irq_handler(int irq, void *ctx) {
    (void)irq;
    e1000_device_t *dev = (e1000_device_t *)ctx;
    reg_read(E1000_REG_ICR);
    dev->irq_fired = 1;
}

int e1000_init(u8 mac_out[6]) {
    pci_device_t dev;
    if (!pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID, &dev)) {
        g_ready = 0;
        return 0;
    }

    g_dev.pci = dev;
    g_dev.irq = dev.irq_line;
    pci_enable_bus_master(&dev);

    int is_mmio = 0;
    u64 bar0 = pci_get_bar(&dev, 0, &is_mmio);
    if (!is_mmio || bar0 == 0) {
        g_ready = 0;
        return 0;
    }

    g_dev.regs_phys = bar0;
    g_dev.regs = (volatile u32 *)phys_to_virt(bar0);

    e1000_read_mac(g_dev.mac);
    e1000_configure_mac();

    u32 ctrl = reg_read(E1000_REG_CTRL);
    ctrl |= (1u << 6);
    ctrl |= (1u << 5);
    reg_write(E1000_REG_CTRL, ctrl);

    reg_write(E1000_REG_IMC, 0xFFFFFFFFu);
    reg_read(E1000_REG_ICR);

    if (!e1000_init_rx() || !e1000_init_tx()) {
        g_ready = 0;
        return 0;
    }

    interrupts_set_irq_handler(g_dev.irq, e1000_irq_handler, &g_dev);
    interrupts_unmask_irq(g_dev.irq);
    reg_write(E1000_REG_IMS, 0x1F6u);

    if (mac_out) {
        for (int i = 0; i < 6; i++) mac_out[i] = g_dev.mac[i];
    }
    g_ready = 1;
    return 1;
}

void e1000_set_rx_callback(e1000_rx_cb cb) {
    g_dev.rx_cb = cb;
}

void e1000_poll(void) {
    if (!g_ready) return;
    if (g_dev.irq_fired) {
        g_dev.irq_fired = 0;
    }

    while (g_dev.rx_descs[g_dev.rx_index].status & 0x1) {
        struct rx_desc *desc = &g_dev.rx_descs[g_dev.rx_index];
        u16 length = desc->length;
        if (length > 0 && g_dev.rx_cb) {
            g_dev.rx_cb(g_dev.rx_bufs[g_dev.rx_index], length);
        }
        desc->status = 0;
        reg_write(E1000_REG_RDT, g_dev.rx_index);
        g_dev.rx_index = (g_dev.rx_index + 1) % RX_DESC_COUNT;
    }
}

int e1000_send(const void *data, u16 len) {
    if (!g_ready || len == 0) return 0;
    if (len > TX_BUF_SIZE) len = TX_BUF_SIZE;
    struct tx_desc *desc = &g_dev.tx_descs[g_dev.tx_index];
    if ((desc->status & 0x1) == 0) return 0;

    memcpy(g_dev.tx_bufs[g_dev.tx_index], data, len);
    desc->length = len;
    desc->cmd = (1u << 0) | (1u << 3);
    desc->status = 0;

    g_dev.tx_index = (g_dev.tx_index + 1) % TX_DESC_COUNT;
    reg_write(E1000_REG_TDT, g_dev.tx_index);
    return 1;
}
