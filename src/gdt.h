/**
 * @file        gdt.h
 * @brief       Global Descriptor Table (GDT) management and structure definitions.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 27, 2026
 * 
 * @details     
 * Provides structural layouts for the GDT pointer register (GDTR) 
 * and modern 64-bit segment descriptors. It establishes standard flat-model 
 * kernel and user space segments required to clear the processor's 
 * real-mode inheritance during x86_64 initialization.
 * 
 * References:
 * - GDT: https://wiki.osdev.org/Global_Descriptor_Table
 * - TSS: https://wiki.osdev.org/Task_State_Segment
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "lib/string.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup CPU_SEGMENTATION CPU Segmentation & Task State Management
 * @brief x86_64 Global Descriptor Table (GDT) and Task State Segment (TSS) management.
 *
 * @details
 * Provides structures, descriptor encoders, and initialization routines
 * for x86_64 segmentation infrastructure used by the kernel.
 *
 * This subsystem configures:
 * - Global Descriptor Table (GDT)
 * - Kernel and user privilege segments
 * - Task State Segment (TSS)
 * - Ring transition stack switching
 * - Interrupt Stack Table (IST) support
 *
 * Although segmentation is largely disabled in x86_64 long mode,
 * valid descriptor tables remain mandatory for:
 * - Privilege level enforcement
 * - System descriptors
 * - Interrupt/syscall transitions
 * - Task register management
 *
 * @{
 */

/**
 * @typedef struct GDTDescriptor
 * @brief  GDTR Register Layout Structure.
 * 
 * @details 
 * This structure maps directly to the hardware format required by the 
 * `lgdt` assembly instruction. It tells the CPU where the GDT array resides 
 * and how large it is.
 */
typedef struct GDTDescriptor
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GDTDescriptor;

/**
 * @typedef struct SegmentDescriptor
 * @brief Standard 64-bit GDT segment descriptor
 *
 * @details 
 * Represents the legacy 8-byte segment descriptor format used by the
 * x86 processor family. Even though segmentation is largely disabled
 * in x86_64 long mode, valid descriptors are still required for:
 *
 * - Kernel and user privilege levels
 * - Code segment execution metadata
 * - System descriptors such as the TSS
 * - Compatibility with interrupt/syscall transitions
 *
 * The descriptor layout is defined by Intel hardware and split into
 * multiple packed bitfields. The CPU reconstructs the final descriptor
 * values internally when the GDT is loaded.
 *
 * In long mod:
 * - Base addresses are ignored for normal code/data segments
 * - Segment limits are ignored
 * - The L-bit inside the granularity field enables 64-bit code execution
 *
 * Descriptor Layout:
 * @code
 *  63                              32 31                      0
 * +---------------------------------+--------------------------+
 * | Base 31:24 | Flags | Limit19:16| Access | Base23:16       |
 * +---------------------------------+--------------------------+
 * | Base15:0                     | Limit15:0                  |
 * +-----------------------------------------------------------+
 * @endcode
 *
 * Common Access Values:
 * - 0x9A : Kernel code segment
 * - 0x92 : Kernel data segment
 * - 0xFA : User code segment
 * - 0xF2 : User data segment
 *
 * Common Flag Values:
 * - 0x20 : 64-bit code segment (L-bit set)
 * - 0x00 : Standard data segment
 */
typedef struct SegmentDescriptor
{
    uint16_t limit_low; /**< Lower 16 bits of the segment limit */
    uint16_t base_low;  /**< Lower 16 bits of the segment base address */
    uint8_t base_middle; /**< Middle 8 bits of th segment base address */

    /**
     * @brief Access control byte
     *
     * Controls:
     * - Descriptor presence
     * - Privilege level (ring)
     * - Executable/data type
     * - Read/Write permissions
     */
    uint8_t access;


    /**
     * @brief Granularity and upper limit field
     *
     * Bit Layout:
     * @code
     * 7   6   5   4   3 2 1 0
     * G | D | L |AVL| LimitHigh
     * @endcode
     *
     * In long mode:
     * - L enables 64-bit code segments
     * - G/D are mostly ignored
     * - Lower nibble stores limit bits 16-19
     */
    uint8_t granularity;
    uint8_t base_high; /**< Upper 8 bits of the segment base address */
} __attribute__((packed)) SegmentDescriptor;

/**
 * @typedef struct TSSEntry
 * @brief x86_64 Task State Segment structure
 * 
 * @details 
 * Defines the hardware Task State Segment (TSS) used by the CPU in
 * x86_64 long mode.
 * 
 * TSS is primarly responsible for:
 * 
 * - Ring 3 -> Ring 0 stack switching
 * - Interrupt Stack Table (IST) support
 * - Dedicated exception recovery stacks
 * - Syscall and interrupt privilege transitions
 * 
 * rsp0 field specifies the kernel stack pointer the CPU loads whenever
 * execution transitions from user mode into kernel mode.
 * 
 * The Interrupt Stack Table (IST) allows specific interrupts to
 * automatically switch to dedicated emergency stacks. This is commonly
 * used for:
 * 
 * - Double faults
 * - Non-maskable interrupts
 * - Critical exception recovery
 * 
 * This structure layout is defined by Intel and must remain packed.
 * 
 */
typedef struct TSSEntry
{
    uint32_t reserved0;

    uint64_t rsp0; /**< Ring 0 kernel stack pointer */
    uint64_t rsp1; /**< Ring 1 stack pointer */
    uint64_t rsp2; /**< Ring 2 stack pointer */

    uint64_t reserved1;

    /**
     * @brief Interrupt Stack Table entries.
     *
     * Each IST entry specifies an alternate stack pointer that may
     * be automatically loaded by the CPU during interrupt delivery.
     */
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;

    uint16_t reserved3;

    /**
     * @brief Offset to the optional I/O permission bitmap.
     *
     * Setting this equal to sizeof(TSSEntry) disables the bitmap.
     */
    uint16_t iomap_base;
} __attribute__((packed)) TSSEntry;

/**
 * @typedef struct TSSDescriptor
 * @brief 64-bit Task State Segment GDT descriptor
 * 
 * @details
 * Represents the special 16-bit system descriptor used to reference
 * an x86_64 Task State Segment from the GDT.
 * 
 * Unlike normal segment descriptors, TSS descriptors occupy TWO
 * consecutive GDT entries (16 bytes total).
 * 
 * The descriptor stores:
 * - Full 64-bits TSS base address
 * - TSS structure size
 * - Descriptor type metadata
 * 
 * The descriptor is loaded into the Task Register (TR) using the
 * `ltr` instruction.
 * 
 */
typedef struct TSSDescriptor
{
    uint16_t limit_low;
    uint16_t base_low;

    uint8_t base_middle1;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_middle2;

    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed)) TSSDescriptor;

/**
 * @brief Initializes the Global Descriptor Table and Task State Segment
 * 
 * @details
 * Builds the kernel's primary GDT containing:
 * 
 * - Null descriptor
 * - Kernel code segment
 * - Kernel data segment
 * - User data segment
 * - User code segment
 * - 64-bit TSS descriptor
 * 
 * After construction:
 * - The GDTR register is loaded via `lgdt`
 * - Segment registers are reloaded
 * - The Task Register (TR) is loded using `ltr`
 * 
 * This function finalizes the CPU segmentation environment used by
 * the kernel after bootloader handoff.
 * 
 */
void InitGDT();

/**
 * @brief Creates a standard 64-bit GDT segment descriptor
 * 
 * @param index   GDT entry index
 * @param base    Segment base address
 * @param limit   Segment size limit
 * @param access  Descriptor access byte
 * @param flags   Descriptor flag bits
 * 
 * @details
 * Encodes a legacy 8-byte segment descriptor directly into the
 * kernel GDT.
 * 
 * In long mode:
 * - Base and limit are largely ignored
 * - The access byte and L-bit remain important
 * 
 * This function is used for normal code and data descriptors
 */
void SetGDTEntry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);

/** @} */

#ifdef __cplusplus
}
#endif

extern TSSEntry tss;
