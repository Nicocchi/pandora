/**
 * @file        idt.cpp
 * @brief       Interrupt Descriptor Table (IDT) initialization and interrupt dispatching.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 29, 2026
 * 
 * @details     
 * Implements the x86_64 Interrupt Descriptor Table subsystem, including:
 * 
 * - IDT entry creation and initialization
 * - PIC remapping for hardware IRQ support
 * - CPU exception handling and diagnostics
 * - IRQ acknowledgement through End Of Interrupt (EOI) signaling
 * - Interrupt dispatch bridging between assembly ISR stubs and C++ handlers
 * 
 * The IDT is loaded through the `lidt` instruction and used by the processor
 * to dispatch exceptions and interrupts into kernel-defined handlers.
 * 
 * Hardware IRQs are remapped away from CPU exception vectors using the
 * legacy Programmable Interrupt Controller (PIC).
 * 
 * References:
 * - IDT: https://wiki.osdev.org/Interrupt_Descriptor_Table
 * - PIC: https://wiki.osdev.org/8259_PIC
 * - Exceptions: https://wiki.osdev.org/Exceptions
 * - Interrupts Tutorial: https://wiki.osdev.org/Interrupts_Tutorial
 */

#include "idt.h"
#include "lib/string.h"
#include "boot/limine_vga.h"
#include "common.h"

/**
 * @brief Global Interrupt Descriptor Table.
 */
struct IDTEntry idt[256];

/**
 * @brief IDTR structure used by lidt.
 */
struct IDTPtr idtr;

/**
 * @brief External assembly routine that loads the IDT.
 */
extern "C" void idt_flush(IDTPtr* idtr);

extern "C"
{
    void isr0();
    void isr1();
    void isr2();
    void isr3();
    void isr4();
    void isr5();
    void isr6();
    void isr7();
    void isr8();
    void isr9();
    void isr10();
    void isr11();
    void isr12();
    void isr13();
    void isr14();
    void isr15();
    void isr16();
    void isr17();
    void isr18();
    void isr19();
    void isr20();
    void isr21();
    void isr22();
    void isr23();
    void isr24();
    void isr25();
    void isr26();
    void isr27();
    void isr28();
    void isr29();
    void isr30();
    void isr31();

    void irq0();
    void irq1();
    void irq2();
    void irq3();
    void irq4();
    void irq5();
    void irq6();
    void irq7();
    void irq8();
    void irq9();
    void irq10();
    void irq11();
    void irq12();
    void irq13();
    void irq14();
    void irq15();
}

/**
 * @brief Exception message lookup table.
 */
const char* exception_messages[] =
{
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-Segment fault",
    "General protection fault",
    "Page falt",
    "Reserved",
    "x87 Floating Point Exception",
    "Alignment Fault",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception"
};



void SetIDTEntry(uint8_t index, uint64_t base, uint16_t selector, uint8_t flags)
{
    IDTEntry *entry = &idt[index];

    entry->offset_low = base & 0xFFFF;
    entry->selector = selector;
    entry->ist = 0;

    entry->flags = flags;

    entry->offset_mid = (base >> 16) & 0xFFFF;
    entry->offset_high = (base >> 32) & 0xFFFFFFFF;

    entry->reserved = 0;
}

/**
 * @brief Remaps the legacy PIC IRQ vectors.
 *
 * Master PIC: IRQs 0-7  -> vectors 32-39
 * Slave PIC:  IRQs 8-15 -> vectors 40-47
 */
static void RemapPIC()
{
    // 0x20 commands and 0x21 data
    // 0xA0 commands and 0xA1 data
    // Start PIC chips into initialization mode
    OutPortB(0x20, 0x11);
    OutPortB(0xA0, 0x11);

    OutPortB(0x21, 0x20);
    OutPortB(0xA1, 0x28);

    OutPortB(0x21, 0x04);
    OutPortB(0xA1, 0x02);

    OutPortB(0x21, 0x01);
    OutPortB(0xA1, 0x01);

    OutPortB(0x21, 0x0);
    OutPortB(0xA1, 0x0);
}

/**
 * @brief Main interrupt dispatcher called from assembly.
 *
 * @param frame Pointer to interrupt register state
 */
extern "C" void interrupt_dispatch(InterruptFrame* frame)
{
    // CPU Exceptions
    if (frame->interrupt_number < 32)
    {
        kprintf("Exception: %s\n", exception_messages[frame->interrupt_number]);
        kprintf("Interrupt #: %llx\n", frame->interrupt_number);
        kprintf("Error Code: %llx\n", frame->error_code);
        kprintf("RIP: %llx\n", frame->rip);
        kprintf("RSP: %p\n", (void*)frame->rsp);
        kprintf("CS: %llx\n", frame->cs);
        kprintf("RFLAGS: %llx\n", frame->rflags);
        hcf();
    }

    // Hardware IRQs
    if (frame->interrupt_number >= 32 && frame->interrupt_number <= 47)
    {
        // Send EOI to slave PIC
        if (frame->interrupt_number >= 40) OutPortB(0xA0, 0x20);

        // Send EOI to master PIC
        OutPortB(0x20, 0x20);
    }
}

void InitIDT()
{
    // memset(&idt, 0, sizeof(IDTEntry) * 256);
    memset(idt, 0, sizeof(idt));

    // idtPtr.limit = sizeof(struct IDTPtr) * 256 - 1;
    idtr.limit = sizeof(IDTEntry) * 256 - 1;
    idtr.base = (uint64_t)&idt;


    
    // CPU Exceptions
    SetIDTEntry(0, (uint64_t)isr0, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(1, (uint64_t)isr1, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(2, (uint64_t)isr2, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(3, (uint64_t)isr3, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(4, (uint64_t)isr4, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(5, (uint64_t)isr5, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(6, (uint64_t)isr6, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(7, (uint64_t)isr7, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(8, (uint64_t)isr8, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(9, (uint64_t)isr9, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(10, (uint64_t)isr10, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(11, (uint64_t)isr11, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(12, (uint64_t)isr12, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(13, (uint64_t)isr13, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(14, (uint64_t)isr14, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(15, (uint64_t)isr15, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(16, (uint64_t)isr16, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(17, (uint64_t)isr17, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(18, (uint64_t)isr18, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(19, (uint64_t)isr19, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(20, (uint64_t)isr20, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(21, (uint64_t)isr21, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(22, (uint64_t)isr22, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(23, (uint64_t)isr23, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(24, (uint64_t)isr24, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(25, (uint64_t)isr25, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(26, (uint64_t)isr26, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(27, (uint64_t)isr27, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(28, (uint64_t)isr28, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(29, (uint64_t)isr29, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(30, (uint64_t)isr30, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(31, (uint64_t)isr31, GDT_KERNEL_CODE, 0x8E);

    // Pic IRQs
    SetIDTEntry(32, (uint64_t)irq0, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(33, (uint64_t)irq1, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(34, (uint64_t)irq2, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(35, (uint64_t)irq3, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(36, (uint64_t)irq4, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(37, (uint64_t)irq5, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(38, (uint64_t)irq6, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(39, (uint64_t)irq7, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(40, (uint64_t)irq8, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(41, (uint64_t)irq9, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(42, (uint64_t)irq10, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(43, (uint64_t)irq11, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(44, (uint64_t)irq12, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(45, (uint64_t)irq13, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(46, (uint64_t)irq14, GDT_KERNEL_CODE, 0x8E);
    SetIDTEntry(47, (uint64_t)irq15, GDT_KERNEL_CODE, 0x8E);

    RemapPIC();

    idt_flush(&idtr);
    
    asm volatile("sti");
}
