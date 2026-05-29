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

/**
 * @brief Kernel-mode GDT segment selectors.
 *
 * These selectors reference ring 0 descriptors installed in the
 * Global Descriptor Table.
 */
constexpr uint16_t GDT_KERNEL_CODE = 0x08;
constexpr uint16_t GDT_KERNEL_DATA = 0x10;

/**
 * @brief User-mode GDT segment selectors.
 *
 * These selectors reference ring 3 descriptors installed in the
 * Global Descriptor Table.
 */
constexpr uint16_t GDT_USER_DATA = 0x18;
constexpr uint16_t GDT_USER_CODE = 0x20;

/**
 * @brief User-mode selectors with Requested Privilege Level (RPL) set.
 *
 * The lower two selector bits encode the privilege level requested
 * by software. Ring 3 selectors are required during transitions
 * into userspace execution.
 */
constexpr uint16_t GDT_USER_DATA_RING3 = 0x1B;
constexpr uint16_t GDT_USER_CODE_RING3 = 0x23;

struct InterruptRegisters
{
    uint32_t cr2;
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, csm, eflags, useresp, ss;
};

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