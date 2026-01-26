#include "interrupts.h"
#include "cpu.h"
#include "input.h"

#define IDT_SIZE 256
#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_COMMAND PIC1
#define PIC1_DATA (PIC1 + 1)
#define PIC2_COMMAND PIC2
#define PIC2_DATA (PIC2 + 1)

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct interrupt_frame {
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

static struct idt_entry idt[IDT_SIZE];
static u16 code_selector = 0x08;
static irq_handler_t irq_handlers[16];
static void *irq_contexts[16];

static void fatal_halt(void) {
    asm volatile("cli");
    while (1) {
        asm volatile("hlt");
    }
}

static void idt_set_gate(int n, void (*handler)(void)) {
    u64 addr = (u64)handler;
    idt[n].offset_low = addr & 0xFFFF;
    idt[n].selector = code_selector;
    idt[n].ist = 0;
    idt[n].type_attr = 0x8E;
    idt[n].offset_mid = (addr >> 16) & 0xFFFF;
    idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
    idt[n].zero = 0;
}

static void idt_load(void) {
    struct idt_ptr idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u64)&idt;
    asm volatile("lidt %0" : : "m"(idtr));
}

static void pic_remap(void) {
    u8 a1 = inb(PIC1_DATA);
    u8 a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

static void pic_send_eoi(int irq) {
    if (irq >= 8) outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}

static void irq_dispatch(int irq) {
    if (irq >= 0 && irq < 16 && irq_handlers[irq]) {
        irq_handlers[irq](irq, irq_contexts[irq]);
    }
    pic_send_eoi(irq);
}

__attribute__((interrupt))
static void isr_default_noerr(struct interrupt_frame *frame) {
    (void)frame;
    fatal_halt();
}

__attribute__((interrupt))
static void isr_default_err(struct interrupt_frame *frame, u64 error_code) {
    (void)frame;
    (void)error_code;
    fatal_halt();
}

__attribute__((interrupt))
static void isr_irq2(struct interrupt_frame *frame) { (void)frame; irq_dispatch(2); }
__attribute__((interrupt))
static void isr_irq3(struct interrupt_frame *frame) { (void)frame; irq_dispatch(3); }
__attribute__((interrupt))
static void isr_irq4(struct interrupt_frame *frame) { (void)frame; irq_dispatch(4); }
__attribute__((interrupt))
static void isr_irq5(struct interrupt_frame *frame) { (void)frame; irq_dispatch(5); }
__attribute__((interrupt))
static void isr_irq6(struct interrupt_frame *frame) { (void)frame; irq_dispatch(6); }
__attribute__((interrupt))
static void isr_irq7(struct interrupt_frame *frame) { (void)frame; irq_dispatch(7); }
__attribute__((interrupt))
static void isr_irq8(struct interrupt_frame *frame) { (void)frame; irq_dispatch(8); }
__attribute__((interrupt))
static void isr_irq9(struct interrupt_frame *frame) { (void)frame; irq_dispatch(9); }
__attribute__((interrupt))
static void isr_irq10(struct interrupt_frame *frame) { (void)frame; irq_dispatch(10); }
__attribute__((interrupt))
static void isr_irq11(struct interrupt_frame *frame) { (void)frame; irq_dispatch(11); }
__attribute__((interrupt))
static void isr_irq12(struct interrupt_frame *frame) { (void)frame; irq_dispatch(12); }
__attribute__((interrupt))
static void isr_irq13(struct interrupt_frame *frame) { (void)frame; irq_dispatch(13); }
__attribute__((interrupt))
static void isr_irq14(struct interrupt_frame *frame) { (void)frame; irq_dispatch(14); }
__attribute__((interrupt))
static void isr_irq15(struct interrupt_frame *frame) { (void)frame; irq_dispatch(15); }

__attribute__((interrupt))
static void isr_timer(struct interrupt_frame *frame) {
    (void)frame;
    timer_handler();
    pic_send_eoi(0);
}

__attribute__((interrupt))
static void isr_keyboard(struct interrupt_frame *frame) {
    (void)frame;
    u8 scancode = inb(0x60);
    input_handle_scancode(scancode);
    pic_send_eoi(1);
}

void interrupts_init(void) {
    asm volatile("mov %%cs, %0" : "=r"(code_selector));

    for (int i = 0; i < IDT_SIZE; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }

    for (int i = 0; i < IDT_SIZE; i++) {
        idt_set_gate(i, (void (*)(void))isr_default_noerr);
    }

    idt_set_gate(8, (void (*)(void))isr_default_err);
    idt_set_gate(10, (void (*)(void))isr_default_err);
    idt_set_gate(11, (void (*)(void))isr_default_err);
    idt_set_gate(12, (void (*)(void))isr_default_err);
    idt_set_gate(13, (void (*)(void))isr_default_err);
    idt_set_gate(14, (void (*)(void))isr_default_err);
    idt_set_gate(17, (void (*)(void))isr_default_err);
    idt_set_gate(21, (void (*)(void))isr_default_err);
    idt_set_gate(30, (void (*)(void))isr_default_err);

    idt_set_gate(32, (void (*)(void))isr_timer);
    idt_set_gate(33, (void (*)(void))isr_keyboard);
    idt_set_gate(34, (void (*)(void))isr_irq2);
    idt_set_gate(35, (void (*)(void))isr_irq3);
    idt_set_gate(36, (void (*)(void))isr_irq4);
    idt_set_gate(37, (void (*)(void))isr_irq5);
    idt_set_gate(38, (void (*)(void))isr_irq6);
    idt_set_gate(39, (void (*)(void))isr_irq7);
    idt_set_gate(40, (void (*)(void))isr_irq8);
    idt_set_gate(41, (void (*)(void))isr_irq9);
    idt_set_gate(42, (void (*)(void))isr_irq10);
    idt_set_gate(43, (void (*)(void))isr_irq11);
    idt_set_gate(44, (void (*)(void))isr_irq12);
    idt_set_gate(45, (void (*)(void))isr_irq13);
    idt_set_gate(46, (void (*)(void))isr_irq14);
    idt_set_gate(47, (void (*)(void))isr_irq15);

    idt_load();
    pic_remap();

    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);

    pit_init(PIT_HZ);
    asm volatile("sti");
}

void interrupts_set_irq_handler(int irq, irq_handler_t handler, void *ctx) {
    if (irq < 0 || irq >= 16) return;
    irq_handlers[irq] = handler;
    irq_contexts[irq] = ctx;
}

void interrupts_unmask_irq(int irq) {
    if (irq < 0 || irq >= 16) return;
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 mask = inb(port);
    mask &= (u8)~(1u << (irq & 7));
    outb(port, mask);
}

void interrupts_mask_irq(int irq) {
    if (irq < 0 || irq >= 16) return;
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 mask = inb(port);
    mask |= (u8)(1u << (irq & 7));
    outb(port, mask);
}
