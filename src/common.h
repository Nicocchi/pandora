/**
 * @file        common.h
 * @brief       Shared kernel constants and low-level architecture resources.
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 28, 2026
 *
 * @details
 * Provides globally shared constants, descriptor selectors,
 * and architecture-level resources used throughout the kernel.
 *
 * This header primarily contains:
 * - GDT selector definitions
 * - Privilege-level segment constants
 * - Early kernel bootstrap resources
 * - Shared low-level CPU infrastructure data
 *
 * The selector values defined here correspond directly to the
 * descriptor layout installed by the kernel Global Descriptor
 * Table (GDT).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "drivers/serial_port.h"
#include "lib/stdio.h"
#include "boot/limine_vga.h"

/**
 * @brief Kernel-mode GDT segment selectors.
 *
 * These selectors reference ring 0 descriptors installed in the
 * Global Descriptor Table.
 */
constexpr uint16_t GDT_KERNEL_CODE = 0x08;
constexpr uint16_t GDT_KERNEL_DATA = 0x10;


/**
 * @brief User-mode selectors with Requested Privilege Level (RPL) set.
 *
 * The lower two selector bits encode the privilege level requested
 * by software. Ring 3 selectors are required during transitions
 * into userspace execution.
 */
constexpr uint16_t GDT_USER_DATA = 0x1B;
constexpr uint16_t GDT_USER_CODE = 0x23;

struct InterruptRegisters
{
    uint32_t cr2;
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, csm, eflags, useresp, ss;
};

static inline uint8_t InPortB(uint16_t port)
{
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void OutPortB(uint16_t port, uint8_t value)
{
    asm volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

/**
 * @brief       Halts the CPU infinitely.
 * @details     Issues the x86 `hlt` instruction to suspend execution until the next 
 *              non-maskable or external hardware interrupt occurs. Wrapped inside an 
 *              infinite fallback loop to prevent processor runaways.
 */
static void hcf(void)
{
    
    for (;;) {
        asm ("hlt");
    }
}

static void IOWait()
{
    OutPortB(0x80, 0);
}

struct PanicContext
{
    const char* message;

    uint64_t interrupt_number;
    uint64_t error_code;

    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;

    uint64_t cs;
    uint64_t ss;
    uint64_t rflags;
};

static void KernelPanic(const PanicContext& ctx)
{
    ClearScreen(BLUE, true);
    kprintf("\n=== KERNEL PANIC ===\n\n");
    kprintf("Reason: %s\n", ctx.message);

    kprintf("Interrupt #: %llu\n", ctx.interrupt_number);
    kprintf("Error Code: %llu\n", ctx.error_code);
    kprintf("RIP: %016llX\n", ctx.rip);
    kprintf("RSP: %016llX\n", ctx.rsp);
    kprintf("RBP: %016llX\n", ctx.rbp);
    kprintf("CS: %016llX\n", ctx.cs);
    kprintf("SS: %016llX\n", ctx.ss);
    kprintf("RFLAGS: %016llX\n", ctx.rflags);

    SerialWriteString(COM1_PORT, "=== KERNEL PANIC ===\n");
    SerialWriteString(COM1_PORT, "Reason: %s\n", ctx.message);
    SerialWriteString(COM1_PORT, "Interrupt #: %llu\n", ctx.interrupt_number);
    SerialWriteString(COM1_PORT, "Error Code: %llu\n", ctx.error_code);
    SerialWriteString(COM1_PORT, "RIP: %016llX\n", ctx.rip);
    SerialWriteString(COM1_PORT, "RSP: %016llX\n", ctx.rsp);
    SerialWriteString(COM1_PORT, "RBP: %016llX\n", ctx.rbp);
    SerialWriteString(COM1_PORT, "CS: %016llX\n", ctx.cs);
    SerialWriteString(COM1_PORT, "SS: %016llX\n", ctx.ss);
    SerialWriteString(COM1_PORT, "RFLAGS: %016llX\n", ctx.rflags);
    hcf();
}

extern uint64_t g_hhdm_offset;

inline void* PhysToVirt(uint64_t phys) {
    return (void*)(phys + g_hhdm_offset);
}

inline uint64_t VirtToPhys(void* virt) {
    return (uint64_t)(virt) - g_hhdm_offset;
}