/**
* @file        idt.h
* @brief       Interrupt Descriptor Table (IDT) structures and interrupt subsystem interfaces.
*
* @copyright   Copyright (c) 2026, Nicocchi
* @license     Licensed under the GPL2 License
*
* @author      Nicocchi
* @date        May 29, 2026
*
* @details
* Defines the core x86_64 interrupt management structures and interfaces
* used by the Pandora OS kernel.
*
* This module provides:
* - Interrupt Descriptor Table (IDT) gate definitions
* - IDTR register layout definitions
* - CPU interrupt stack frame structures
* - Interrupt handler registration interfaces
* - CPU interrupt control helpers
* - Legacy PIC compatibility interfaces
*
* The IDT is responsible for routing:
* - CPU exceptions (vectors 0-31)
* - Hardware interrupts (IRQs)
* - Software interrupts
* - APIC-routed interrupt vectors
*
* Interrupts originate from low-level assembly ISR stubs which preserve
* processor state before transferring execution into the higher-level
* C++ interrupt dispatcher.
*
* In APIC mode, hardware interrupt acknowledgement is performed through
* the Local APIC End Of Interrupt (EOI) register instead of the legacy
* 8259 PIC controller.
*
* The structures defined in this file must remain ABI-compatible with:
* - x86_64 interrupt frame layouts
* - ISR assembly stubs
* - the `lidt` instruction requirements
*
* References:
* - Intel® 64 and IA-32 Architectures SDM Vol. 3A:
* - Chapter 6 — Interrupt and Exception Handling
* - https://wiki.osdev.org/Interrupt_Descriptor_Table
* - https://wiki.osdev.org/Exceptions
* - https://wiki.osdev.org/APIC
* - https://wiki.osdev.org/8259_PIC
*/

#pragma once

#include <stdint.h>
#include <stddef.h>

/**

* @brief A single Interrupt Descriptor Table (IDT) gate descriptor.
*
* Represents a 16-byte interrupt gate entry in the x86_64 IDT.
*
* Each descriptor defines:
* - the target interrupt service routine address
* - the target code segment selector
* - gate privilege attributes
* - optional Interrupt Stack Table (IST) switching
*
* The processor indexes this structure using the interrupt vector number
* during interrupt or exception dispatch.
*
* Structure layout follows the Intel-defined 64-bit interrupt gate format.
*/
typedef struct IDTEntry
{
    uint16_t offset_low;    /**< ISR address bits 0-15 */
    uint16_t selector;      /**< Kernel code segment from the GDT */

   /**
    *  @brief Interrupt Stack Table (IST) index.
    *
    * Bits 0-2 select an IST entry from the Task State Segment (TSS).
    * A value of 0 disables IST switching.
    */
    uint8_t ist;

    /**
    * @brief Gate type and attribute flags.
    *
    * Common value:
    * - 0x8E = Present | Ring 0 | 64-bit Interrupt Gate
    */
    uint8_t flags;

    uint16_t offset_mid;    /**< ISR address bits 16-31 */
    uint32_t offset_high;   /**< ISR address bits 32-63 */

    uint32_t reserved;      /**< Reserved field */
} __attribute__((packed)) IDTEntry;

/**
 * @brief Structure used by the lidt instruction.
 */
typedef struct IDTPtr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) IDTPtr;

/**
* @brief CPU register state captured during interrupt dispatch.
*
* This structure represents the complete processor state preserved by
* the low-level ISR assembly stubs before control is transferred into
* the C++ interrupt dispatcher.
*
* The layout MUST exactly match the push order implemented in `idt.asm`.
* Any modification to this structure requires corresponding assembly updates.
*
* The frame contains:
* - General-purpose registers
* - Interrupt vector number
* - CPU-generated error code
* - Automatically-pushed processor state
*
* The final fields (`rip`, `cs`, and `rflags`) are automatically pushed
* by the processor during interrupt or exception entry.
*
* For privilege-level transitions, the CPU may additionally push:
* - RSP
* - SS
*
* Those fields are currently omitted because interrupts originate only
* from ring 0 kernel execution.
*/
typedef struct InterruptFrame
{
    uint64_t r15;                   /**< General-purpose register R15 */
    uint64_t r14;                   /**< General-purpose register R14 */
    uint64_t r13;                   /**< General-purpose register R13 */
    uint64_t r12;                   /**< General-purpose register R12 */
    uint64_t r11;                   /**< General-purpose register R11 */
    uint64_t r10;                   /**< General-purpose register R10 */
    uint64_t r9;                    /**< General-purpose register R9 */
    uint64_t r8;                    /**< General-purpose register R8 */

    uint64_t rbp;                   /**< Base pointer register */
    uint64_t rdi;                   /**< First function argument register */
    uint64_t rsi;                   /**< Second function argument register */
    uint64_t rdx;                   /**< Third function argument register */
    uint64_t rcx;                   /**< Fourth function argument register */
    uint64_t rbx;                   /**< General-purpose register RBX */
    uint64_t rax;                   /**< Return value register */

    uint64_t interrupt_number;      /**< Interrupt vector number */
    uint64_t error_code;            /**< CPU-generated or synthetic error code */

    uint64_t rip;                   /**< Instruction pointer at interrupt */
    uint64_t cs;                    /**< Code segment selector */
    uint64_t rflags;                /**< Processor flags register */
} __attribute__((packed)) InterruptFrame;

/**
 * @brief Initializes the Interrupt Descriptor Table.
 */
void InitIDT();

/**
 * @brief Configures a single IDT entry.
 *
 * @param index     IDT vector index
 * @param base      Address of ISR handler
 * @param sel       Code segment selector
 * @param flags     Gate flags and attributes
 */
void SetIDTEntry(uint8_t index, uint64_t base, uint16_t sel, uint8_t flags);

typedef void (*InterruptHandler)(InterruptFrame* frame);

/**
* @brief Registers a high-level interrupt handler.
*
* Associates a C++ callback with a specific interrupt vector.
*
* The handler will be invoked by the interrupt dispatcher after
* low-level CPU state preservation has completed.
*
* @param interrupt   Interrupt vector number
* @param handler     Callback function to invoke
*/
void RegisterInterruptHandler(uint8_t interrupt, InterruptHandler handler);

/**
* @brief Unregisters a high-level interrupt handler.
*
*
* The handler will be invoked by the interrupt dispatcher after
* low-level CPU state preservation has completed.
*
* @param interrupt   Interrupt vector number
*/
void UnregisterInterruptHandler(uint8_t interrupt);

/**
* @brief Enables interrupts
*/
void EnableInterrupts();

/**
* @brief Disables the legacy 8259 PIC controller.
*
* Masks all IRQ lines on both the master and slave PICs.
*
* This is required when transitioning to APIC mode in order to
* prevent duplicate interrupt delivery from the legacy PIC hardware.
*/
void DisablePIC();