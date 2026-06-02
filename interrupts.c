#include "../include/kernel.h"
#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_EOI      0x20

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t interrupt_number;
    uint64_t error_code;
} isr_frame_t;

#define IDT_GATES 256

static idt_entry_t idt[IDT_GATES] __attribute__((aligned(16)));
static idt_ptr_t idt_ptr;

typedef void (*interrupt_handler_t)(void);
static interrupt_handler_t interrupt_handlers[IDT_GATES];

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void irq32(void);
extern void irq33(void);
extern void irq34(void);
extern void irq35(void);
extern void irq36(void);
extern void irq37(void);
extern void irq38(void);
extern void irq39(void);
extern void irq40(void);
extern void irq41(void);
extern void irq42(void);
extern void irq43(void);
extern void irq44(void);
extern void irq45(void);
extern void irq46(void);
extern void irq47(void);

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void set_idt_gate(int vector, uint64_t handler) {
    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].zero = 0;
}

static void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    outb(PIC2_COMMAND, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);
    outb(PIC1_DATA, 0x00);
    outb(PIC2_DATA, 0x00);
}

static inline void pic_send_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

void idt_initialize(void) {
    for (int i = 0; i < IDT_GATES; i++) {
        set_idt_gate(i, 0);
        interrupt_handlers[i] = NULL;
    }

    pic_remap();

    set_idt_gate(0, (uint64_t)isr0);
    set_idt_gate(1, (uint64_t)isr1);
    set_idt_gate(2, (uint64_t)isr2);
    set_idt_gate(3, (uint64_t)isr3);
    set_idt_gate(4, (uint64_t)isr4);
    set_idt_gate(5, (uint64_t)isr5);
    set_idt_gate(6, (uint64_t)isr6);
    set_idt_gate(7, (uint64_t)isr7);
    set_idt_gate(8, (uint64_t)isr8);
    set_idt_gate(9, (uint64_t)isr9);
    set_idt_gate(10, (uint64_t)isr10);
    set_idt_gate(11, (uint64_t)isr11);
    set_idt_gate(12, (uint64_t)isr12);
    set_idt_gate(13, (uint64_t)isr13);
    set_idt_gate(14, (uint64_t)isr14);
    set_idt_gate(15, (uint64_t)isr15);
    set_idt_gate(16, (uint64_t)isr16);
    set_idt_gate(17, (uint64_t)isr17);
    set_idt_gate(18, (uint64_t)isr18);
    set_idt_gate(19, (uint64_t)isr19);
    set_idt_gate(20, (uint64_t)isr20);

    set_idt_gate(32, (uint64_t)irq32);
    set_idt_gate(33, (uint64_t)irq33);
    set_idt_gate(34, (uint64_t)irq34);
    set_idt_gate(35, (uint64_t)irq35);
    set_idt_gate(36, (uint64_t)irq36);
    set_idt_gate(37, (uint64_t)irq37);
    set_idt_gate(38, (uint64_t)irq38);
    set_idt_gate(39, (uint64_t)irq39);
    set_idt_gate(40, (uint64_t)irq40);
    set_idt_gate(41, (uint64_t)irq41);
    set_idt_gate(42, (uint64_t)irq42);
    set_idt_gate(43, (uint64_t)irq43);
    set_idt_gate(44, (uint64_t)irq44);
    set_idt_gate(45, (uint64_t)irq45);
    set_idt_gate(46, (uint64_t)irq46);
    set_idt_gate(47, (uint64_t)irq47);

    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_GATES) - 1;
    idt_ptr.base = (uint64_t)&idt;
    asm volatile("lidt %0" :: "m"(idt_ptr));

    KLOG("IDT initialized with %d gates", IDT_GATES);
}

void set_interrupt_handler(int irq, void (*handler)(void)) {
    if (irq < 0 || irq >= IDT_GATES) {
        return;
    }
    interrupt_handlers[irq] = handler;
}

void pit_initialize(uint32_t frequency) {
    uint16_t divisor = (uint16_t)(1193180 / frequency);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void enable_interrupts(void) {
    asm volatile("sti");
}

void disable_interrupts(void) {
    asm volatile("cli");
}

void isr_handler(uint64_t* stack_frame) {
    isr_frame_t* frame = (isr_frame_t*)stack_frame;
    uint32_t irq = (uint32_t)frame->interrupt_number;

    if (irq >= 32 && irq < 48) {
        if (interrupt_handlers[irq]) {
            interrupt_handlers[irq]();
        }
        pic_send_eoi(irq - 32);
        return;
    }

    if (interrupt_handlers[irq]) {
        interrupt_handlers[irq]();
        return;
    }

    KERROR("Unhandled interrupt %u, error code 0x%llx", irq, frame->error_code);
}
