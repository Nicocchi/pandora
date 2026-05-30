/**
 * @file        idt.cpp
 * @brief       Interrupt Descriptor Table initialization and interrupt dispatch subsystem.
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 29, 2026
 *
 * @details
 * Implements the x86_64 interrupt handling subsystem for Pandora OS.
 *
 * Responsibilities include:
 * - IDT construction and initialization
 * - Exception vector installation
 * - Hardware IRQ vector installation
 * - Interrupt dispatch bridging
 * - Interrupt handler registration
 * - CPU exception diagnostics
 * - APIC End Of Interrupt (EOI) signaling
 * - Legacy PIC remapping and disabling
 *
 * Interrupt handling flow:
 * 1. Hardware or CPU exception occurs
 * 2. CPU indexes the IDT using the interrupt vector
 * 3. Assembly ISR stub preserves CPU state
 * 4. Control transfers into `interrupt_dispatch()`
 * 5. Registered C++ handlers are invoked
 * 6. APIC EOI signaling completes interrupt servicing
 *
 * In modern APIC mode:
 * - The Local APIC replaces the legacy PIC
 * - IRQs are routed through the I/O APIC
 * - EOIs are sent through the LAPIC EOI register
 *
 * References:
 * - Intel® SDM Vol. 3A Chapter 6
 * - https://wiki.osdev.org/Interrupt_Descriptor_Table
 * - https://wiki.osdev.org/APIC
 * - https://wiki.osdev.org/8259_PIC
 */

#include "idt.h"
#include "lib/string.h"
#include "boot/limine_vga.h"
#include "drivers/serial_port.h"
#include "common.h"
#include "apic.h"

/**
 * @brief Global Interrupt Descriptor Table storage.
 *
 * Contains all 256 interrupt vector descriptors used by the processor.
*/
struct IDTEntry idt[256];

/**
 * @brief IDTR structure used by lidt.
 */
struct IDTPtr idtr;

/**
 * @brief High-level interrupt callback table.
 *
 * Stores optional C++ interrupt handlers indexed by interrupt vector.
 *
 * A nullptr entry indicates no registered handler.
 */
static InterruptHandler interrupt_handlers[256];

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
    "Page fault",
    "Reserved",
    "x87 Floating Point Exception",
    "Alignment Fault",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception"
};

void RegisterInterruptHandler(uint8_t interrupt, InterruptHandler handler)
{
    interrupt_handlers[interrupt] = handler;
}

void UnregisterInterruptHandler(uint8_t interrupt)
{
    interrupt_handlers[interrupt] = nullptr;
}


/**
 * @brief Configures a single IDT gate descriptor.
 *
 * Installs an interrupt or exception handler into the IDT.
 *
 * The descriptor defines:
 * - ISR entry address
 * - target code segment
 * - gate privilege attributes
 * - interrupt gate behavior
 *
 * @param index       Interrupt vector index
 * @param base        ISR function address
 * @param selector    GDT code segment selector
 * @param flags       Descriptor attribute flags
 */

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
 * @brief Remaps legacy PIC IRQ vectors.
 *
 * Reconfigures the master and slave 8259 PIC controllers so hardware
 * IRQs do not overlap with reserved CPU exception vectors.
 *
 * Original PIC mappings:
 * - IRQ0-7  -> vectors 0x08-0x0F
 * - IRQ8-15 -> vectors 0x70-0x77
 *
 * Remapped PIC mappings:
 * - IRQ0-7  -> vectors 32-39
 * - IRQ8-15 -> vectors 40-47
 *
 * Although the kernel primarily operates in APIC mode, PIC remapping
 * remains useful during early initialization and compatibility stages.
 */
static void RemapPIC()
{
    // 0x20 commands and 0x21 data
    // 0xA0 commands and 0xA1 data
    // Start PIC chips into initialization mode
    uint8_t mask1 = InPortB(0x21);
    uint8_t mask2 = InPortB(0xA1);

    OutPortB(0x20, 0x11);
    IOWait();
    OutPortB(0xA0, 0x11);
    IOWait();

    OutPortB(0x21, 0x20);
    IOWait();
    OutPortB(0xA1, 0x28);
    IOWait();

    OutPortB(0x21, 0x04);
    IOWait();
    OutPortB(0xA1, 0x02);
    IOWait();

    OutPortB(0x21, 0x01);
    IOWait();
    OutPortB(0xA1, 0x01);
    IOWait();

    OutPortB(0x21, 0x0);
    IOWait();
    OutPortB(0xA1, 0x0);
    IOWait();
}

void DisablePIC()
{
    // Remap first to avoid conflicts with CPU exceptions
    // then mask everything
    OutPortB(0x20, 0x11); OutPortB(0x80, 0);
    OutPortB(0xA0, 0x11); OutPortB(0x80, 0);
    OutPortB(0x21, 0x20); OutPortB(0x80, 0);
    OutPortB(0xA1, 0x28); OutPortB(0x80, 0);
    OutPortB(0x21, 0x04); OutPortB(0x80, 0);
    OutPortB(0xa1, 0x02); OutPortB(0x80, 0);
    OutPortB(0x21, 0x01); OutPortB(0x80, 0);
    OutPortB(0xA1, 0x01); OutPortB(0x80, 0);

    // Mask all IRQs
    OutPortB(0x21, 0xFF);
    OutPortB(0xA1, 0xFF);
}

/**
 * @brief Central interrupt and exception dispatcher.
 *
 * Invoked directly from low-level ISR assembly stubs after CPU state
 * preservation has completed.
 *
 * Responsibilities:
 * - Exception diagnostics and panic handling
 * - Registered interrupt callback dispatch
 * - Hardware interrupt acknowledgement
 * - APIC End Of Interrupt signaling
 *
 * Exception vectors (< 32) are treated as fatal kernel faults unless
 * explicitly handled elsewhere.
 *
 * Hardware interrupts routed through the APIC subsystem require an
 * explicit LAPIC EOI signal after servicing completes.
 *
 * @param frame Pointer to the preserved interrupt frame.
 */
extern "C" void interrupt_dispatch(InterruptFrame* frame)
{
    uint64_t interrupt = frame->interrupt_number;

    // CPU Exceptions
    if (interrupt < 32)
    {
        kprintf("Exception: %s\n", exception_messages[interrupt]);
        kprintf("Interrupt #: %d\n", interrupt);
        kprintf("Error Code: %d\n", frame->error_code);
        kprintf("RIP: %llx\n", frame->rip);
        // kprintf("RSP: %p\n", (void*)frame->rsp);
        kprintf("CS: %llx\n", frame->cs);
        kprintf("RFLAGS: %llx\n", frame->rflags);

        SerialWriteString(COM1_PORT, "Exception: %s\n", exception_messages[interrupt]);
        SerialWriteString(COM1_PORT, "Interrupt #: %d\n", interrupt);
        SerialWriteString(COM1_PORT, "Error Code: %d\n", frame->error_code);
        SerialWriteString(COM1_PORT, "RIP: %llx\n", frame->rip);
        // SerialWriteString(COM1_PORT, "RSP: %p\n", (void*)frame->rsp);
        SerialWriteString(COM1_PORT, "CS: %llx\n", frame->cs);
        SerialWriteString(COM1_PORT, "RFLAGS: %llx\n", frame->rflags);
        hcf();
    }

    // Register handler
    if (interrupt_handlers[interrupt] != nullptr)
    {
        interrupt_handlers[interrupt](frame);
    }

    // Hardware IRQs
    if (interrupt >= 32)
    {
        /**
         * Hardware interrupts routed through the APIC subsystem require an
         * explicit End Of Interrupt (EOI) signal to the Local APIC.
         *
         * Without an EOI, the APIC will not deliver additional interrupts
         * of the same class.
         */
        LAPICEoi();
    }
    // if (interrupt >= 32 && interrupt <= 47)
    // {
    //     // Send EOI to slave PIC
    //     if (interrupt >= 40) OutPortB(0xA0, 0x20);

    //     // Send EOI to master PIC
    //     OutPortB(0x20, 0x20);
    // }

    

    
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
    
}

void EnableInterrupts()
{
    asm volatile("sti");
}
