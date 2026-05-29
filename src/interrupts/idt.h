/**
 * @file        idt.h
 * @brief       Interrupt Descriptor Table (IDT) structures and interrupt handling definitions.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 29, 2026
 * 
 * @details     
 * Defines the core data structures used for x86_64 interrupt handling,
 * including IDT gate descriptors, the IDTR register structure, and the
 * interrupt stack frame passed from low-level assembly interrupt stubs
 * into the higher-level C++ interrupt dispatcher.
 * 
 * The IDT is responsible for routing CPU exceptions, hardware IRQs,
 * and software interrupts to their respective interrupt service routines (ISRs).
 * 
 * References:
 * - IDT: https://wiki.osdev.org/Interrupt_Descriptor_Table
 * - Exceptions: https://wiki.osdev.org/Exceptions
 * - Interrupts Tutorial: https://wiki.osdev.org/Interrupts_Tutorial
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * @brief A single Interrupt Descriptor Table entry.
 *
 * Represents a 64-bit interrupt gate in the IDT.
 */
typedef struct IDTEntry
{
    uint16_t offset_low;    /**< ISR address bits 0-15 */
    uint16_t selector;      /**< Code segment selector */

    uint8_t ist;            /**< Interrupt Stack Table offest */
    uint8_t flags;          /**< Gate type and attributes */

    uint16_t offset_mid;    /**< ISR address bits 16-31 */
    uint32_t offset_high;   /**< ISR address bits 32-63 */

    uint32_t reserved;      /**< Reserved, must be zero */
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
 * @brief CPU register state captured during an interrupt.
 *
 * This structure MUST exactly match the stack layout created by
 * the ISR assembly stubs in idt.asm.
 */
typedef struct InterruptFrame
{
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

    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;

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