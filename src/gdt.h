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
 * @details     Provides structural layouts for the GDT pointer register (GDTR) 
 *              and modern 64-bit segment descriptors. It establishes standard flat-model 
 *              kernel and user space segments required to clear the processor's 
 *              real-mode inheritance during x86_64 initialization.
 * 
 *              References:
 *              - GDT: https://wiki.osdev.org/Global_Descriptor_Table
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief  GDTR Register Layout Structure.
 * @struct GDTDescriptor
 * @details This structure maps directly to the hardware format required by the 
 *          `lgdt` assembly instruction. It tells the CPU where the GDT array resides 
 *          and how large it is.
 */
typedef struct GDTDescriptor
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) GDTDescriptor;

typedef struct SegmentDescriptor
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;

    uint8_t access;

    uint8_t granularity; // | flags | limit high |

    uint8_t base_high;
} __attribute__((packed)) SegmentDescriptor;



void InitGDT();
void SetGDTGate(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags);

#ifdef __cplusplus
}
#endif
