#include "kernel/interrupts.h"
#include "kernel/cpu.h"
#include "drivers/input.h"
#include "drivers/gfx.h"
#include "kernel/memory.h"
#include "kernel/lapic.h"
#include "kernel/task.h"
#include "services/log.h"
#include "drivers/serial.h"

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

static void serial_write_hex(u64 value) {
    char buf[19];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        u8 nibble = (u8)((value >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (nibble < 10) ? (char)('0' + nibble) : (char)('a' + (nibble - 10));
    }
    buf[18] = 0;
    serial_write_str(buf);
}

static void fatal_exception(int vector, struct interrupt_frame *frame, u64 error_code, int has_error) {
    asm volatile("cli");
    serial_write_str("[PANIC] unhandled exception vector=");
    serial_write_hex((u64)vector);
    serial_write_str("\n  RIP=");
    serial_write_hex(frame->rip);
    serial_write_str("  RSP=");
    serial_write_hex(frame->rsp);
    serial_write_str("\n  RFLAGS=");
    serial_write_hex(frame->rflags);
    serial_write_str("  CS=");
    serial_write_hex(frame->cs);
    serial_write_str("  SS=");
    serial_write_hex(frame->ss);
    if (has_error) {
        serial_write_str("\n  ERROR=");
        serial_write_hex(error_code);
    }
    serial_write_str("\n");
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
    fatal_exception(-1, frame, 0, 0);
}

__attribute__((interrupt))
static void isr_default_err(struct interrupt_frame *frame, u64 error_code) {
    fatal_exception(-1, frame, error_code, 1);
}

static void exception_noerr(int vector, struct interrupt_frame *frame) {
    fatal_exception(vector, frame, 0, 0);
}

static void exception_err(int vector, struct interrupt_frame *frame, u64 error_code) {
    fatal_exception(vector, frame, error_code, 1);
}

__attribute__((interrupt)) static void isr_ex_0(struct interrupt_frame *frame) { exception_noerr(0, frame); }
__attribute__((interrupt)) static void isr_ex_1(struct interrupt_frame *frame) { exception_noerr(1, frame); }
__attribute__((interrupt)) static void isr_ex_2(struct interrupt_frame *frame) { exception_noerr(2, frame); }
__attribute__((interrupt)) static void isr_ex_3(struct interrupt_frame *frame) { exception_noerr(3, frame); }
__attribute__((interrupt)) static void isr_ex_4(struct interrupt_frame *frame) { exception_noerr(4, frame); }
__attribute__((interrupt)) static void isr_ex_5(struct interrupt_frame *frame) { exception_noerr(5, frame); }
__attribute__((interrupt)) static void isr_ex_6(struct interrupt_frame *frame) { exception_noerr(6, frame); }
__attribute__((interrupt)) static void isr_ex_7(struct interrupt_frame *frame) { exception_noerr(7, frame); }
__attribute__((interrupt)) static void isr_ex_8(struct interrupt_frame *frame, u64 error) { exception_err(8, frame, error); }
__attribute__((interrupt)) static void isr_ex_9(struct interrupt_frame *frame) { exception_noerr(9, frame); }
__attribute__((interrupt)) static void isr_ex_10(struct interrupt_frame *frame, u64 error) { exception_err(10, frame, error); }
__attribute__((interrupt)) static void isr_ex_11(struct interrupt_frame *frame, u64 error) { exception_err(11, frame, error); }
__attribute__((interrupt)) static void isr_ex_12(struct interrupt_frame *frame, u64 error) { exception_err(12, frame, error); }
__attribute__((interrupt)) static void isr_ex_13(struct interrupt_frame *frame, u64 error) { exception_err(13, frame, error); }
__attribute__((interrupt)) static void isr_ex_14(struct interrupt_frame *frame, u64 error) { exception_err(14, frame, error); }
__attribute__((interrupt)) static void isr_ex_15(struct interrupt_frame *frame) { exception_noerr(15, frame); }
__attribute__((interrupt)) static void isr_ex_16(struct interrupt_frame *frame) { exception_noerr(16, frame); }
__attribute__((interrupt)) static void isr_ex_17(struct interrupt_frame *frame, u64 error) { exception_err(17, frame, error); }
__attribute__((interrupt)) static void isr_ex_18(struct interrupt_frame *frame) { exception_noerr(18, frame); }
__attribute__((interrupt)) static void isr_ex_19(struct interrupt_frame *frame) { exception_noerr(19, frame); }
__attribute__((interrupt)) static void isr_ex_20(struct interrupt_frame *frame) { exception_noerr(20, frame); }
__attribute__((interrupt)) static void isr_ex_21(struct interrupt_frame *frame, u64 error) { exception_err(21, frame, error); }
__attribute__((interrupt)) static void isr_ex_22(struct interrupt_frame *frame) { exception_noerr(22, frame); }
__attribute__((interrupt)) static void isr_ex_23(struct interrupt_frame *frame) { exception_noerr(23, frame); }
__attribute__((interrupt)) static void isr_ex_24(struct interrupt_frame *frame) { exception_noerr(24, frame); }
__attribute__((interrupt)) static void isr_ex_25(struct interrupt_frame *frame) { exception_noerr(25, frame); }
__attribute__((interrupt)) static void isr_ex_26(struct interrupt_frame *frame) { exception_noerr(26, frame); }
__attribute__((interrupt)) static void isr_ex_27(struct interrupt_frame *frame) { exception_noerr(27, frame); }
__attribute__((interrupt)) static void isr_ex_28(struct interrupt_frame *frame) { exception_noerr(28, frame); }
__attribute__((interrupt)) static void isr_ex_29(struct interrupt_frame *frame) { exception_noerr(29, frame); }
__attribute__((interrupt)) static void isr_ex_30(struct interrupt_frame *frame, u64 error) { exception_err(30, frame, error); }
__attribute__((interrupt)) static void isr_ex_31(struct interrupt_frame *frame) { exception_noerr(31, frame); }

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

__attribute__((interrupt))
static void isr_mouse(struct interrupt_frame *frame) {
    (void)frame;
    u8 data = inb(0x60);
    irq_counts[12]++;
    input_handle_mouse_byte(data);
    pic_send_eoi(12);
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
        "movq %rsp, %r12\n"
        "movq %rsp, %rdi\n"
        "call task_schedule_isr\n"
        "movq %rax, %rbx\n"
        "call lapic_eoi\n"
        "movq %rbx, %rsp\n"
        "testq %rbx, %rbx\n"
        "jz 1f\n"
        "movq 120(%rbx), %rax\n"
        "movq 128(%rbx), %rcx\n"
        "movq 136(%rbx), %r8\n"
        "testq %rcx, %rcx\n"
        "jz 1f\n"
        "testq $0x2, %r8\n"
        "jz 1f\n"
        "movq %rax, %r9\n"
        "shr $48, %r9\n"
        "cmp $0xFFFF, %r9\n"
        "je 2f\n"
        "test %r9, %r9\n"
        "je 2f\n"
        "1:\n"
        "movq %r12, %rsp\n"
        "2:\n"
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

    static void (*const exception_handlers[32])(void) = {
        (void (*)(void))isr_ex_0,  (void (*)(void))isr_ex_1,  (void (*)(void))isr_ex_2,  (void (*)(void))isr_ex_3,
        (void (*)(void))isr_ex_4,  (void (*)(void))isr_ex_5,  (void (*)(void))isr_ex_6,  (void (*)(void))isr_ex_7,
        (void (*)(void))isr_ex_8,  (void (*)(void))isr_ex_9,  (void (*)(void))isr_ex_10, (void (*)(void))isr_ex_11,
        (void (*)(void))isr_ex_12, (void (*)(void))isr_ex_13, (void (*)(void))isr_ex_14, (void (*)(void))isr_ex_15,
        (void (*)(void))isr_ex_16, (void (*)(void))isr_ex_17, (void (*)(void))isr_ex_18, (void (*)(void))isr_ex_19,
        (void (*)(void))isr_ex_20, (void (*)(void))isr_ex_21, (void (*)(void))isr_ex_22, (void (*)(void))isr_ex_23,
        (void (*)(void))isr_ex_24, (void (*)(void))isr_ex_25, (void (*)(void))isr_ex_26, (void (*)(void))isr_ex_27,
        (void (*)(void))isr_ex_28, (void (*)(void))isr_ex_29, (void (*)(void))isr_ex_30, (void (*)(void))isr_ex_31
    };

    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, exception_handlers[i]);
    }

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
    idt_set_gate(44, (void (*)(void))isr_mouse);
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
