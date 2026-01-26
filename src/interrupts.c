#include "interrupts.h"
#include "cpu.h"
#include "input.h"
#include "gfx.h"
#include "memory.h"
#include "lapic.h"
#include "task.h"

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
static volatile u64 irq_counts[16];
static void (*vector_handlers[IDT_SIZE])(void);

static void u64_to_dec_local(char *out, int max, u64 value) {
    if (max <= 1) return;
    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    char temp[24];
    int pos = 0;
    while (value && pos < (int)sizeof(temp)) {
        temp[pos++] = (char)('0' + (value % 10));
        value /= 10;
    }
    int out_pos = 0;
    while (pos > 0 && out_pos < max - 1) {
        out[out_pos++] = temp[--pos];
    }
    out[out_pos] = 0;
}

static void draw_crash_overlay(const char *msg) {
    if (gfx_width() == 0 || gfx_height() == 0) return;
    gfx_clear(0x200000);
    int w = (int)gfx_width();
    int h = (int)gfx_height();
    int box_w = w > 360 ? 360 : w - 24;
    int box_h = 120;
    int x = (w - box_w) / 2;
    int y = (h - box_h) / 2;
    gfx_draw_rect(x, y, box_w, box_h, 0x3A0A0A);
    gfx_draw_rect(x, y, box_w, 2, 0x7A1E1E);
    gfx_draw_text("KERNEL HALT", x + 12, y + 12, 0xF2E9E9);
    if (msg) gfx_draw_text(msg, x + 12, y + 32, 0xF2E9E9);

    char buf[32];
    gfx_draw_text("Ticks:", x + 12, y + 56, 0xE0C9C9);
    u64_to_dec_local(buf, (int)sizeof(buf), ticks);
    gfx_draw_text(buf, x + 84, y + 56, 0xF2E9E9);
    gfx_draw_text("Uptime:", x + 12, y + 74, 0xE0C9C9);
    u64_to_dec_local(buf, (int)sizeof(buf), uptime_seconds);
    gfx_draw_text(buf, x + 84, y + 74, 0xF2E9E9);
    gfx_present();
}

static void fatal_halt(void) {
    asm volatile("cli");
    draw_crash_overlay("Unhandled exception");
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
    if (irq >= 0 && irq < 16) {
        irq_counts[irq]++;
        if (irq_handlers[irq]) {
            irq_handlers[irq](irq, irq_contexts[irq]);
        }
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
    task_tick();
    irq_counts[0]++;
    pic_send_eoi(0);
}

__attribute__((interrupt))
static void isr_keyboard(struct interrupt_frame *frame) {
    (void)frame;
    u8 scancode = inb(0x60);
    irq_counts[1]++;
    input_handle_scancode(scancode);
    pic_send_eoi(1);
}

__attribute__((naked))
static void isr_vector_0xf0(void) {
    asm volatile(
        "cli\n"
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rbp\n"
        "pushq %rdi\n"
        "pushq %rsi\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "movq %rsp, %rdi\n"
        "call task_schedule_isr\n"
        "movq %rax, %rbx\n"
        "call lapic_eoi\n"
        "movq %rbx, %rsp\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rsi\n"
        "popq %rdi\n"
        "popq %rbp\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n"
    );
}

static void interrupts_setup_idt(void) {
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
    idt_set_gate(0xF0, (void (*)(void))isr_vector_0xf0);

    idt_load();
}

void interrupts_init(void) {
    interrupts_setup_idt();
    pic_remap();

    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);

    pit_init(PIT_HZ);
    asm volatile("sti");
}

void interrupts_init_ap(void) {
    interrupts_setup_idt();
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

u64 interrupts_get_irq_count(int irq) {
    if (irq < 0 || irq >= 16) return 0;
    return irq_counts[irq];
}

void interrupts_set_vector(int vector, void (*handler)(void)) {
    if (vector < 0 || vector >= IDT_SIZE) return;
    vector_handlers[vector] = handler;
    idt_set_gate(vector, handler);
}
