/**
 * @file        gdt.cpp
 * @brief       Global Descriptor Table (GDT) and Task State Segment (TSS)
 *              initialization routines.
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 27, 2026
 *
 * @ingroup     CPU_SEGMENTATION
 *
 * @details
 * Implements construction and installation of the kernel's x86_64
 * descriptor table infrastructure.
 *
 * This module is responsible for:
 * - Building the Global Descriptor Table (GDT)
 * - Creating kernel and user segment descriptors
 * - Constructing the 64-bit Task State Segment (TSS) descriptor
 * - Initializing the Task State Segment structure
 * - Loading the GDTR register using `lgdt`
 * - Loading the Task Register (TR) using `ltr`
 * - Reloading CPU segment registers
 *
 * The initialized GDT establishes a flat-memory segmentation model
 * suitable for modern x86_64 long mode kernels.
 *
 * Although segmentation is mostly disabled in long mode, descriptor
 * tables remain required for:
 * - Privilege level separation
 * - System segment management
 * - Ring transitions
 * - Interrupt stack switching
 * - Task State Segment support
 *
 * The TSS is configured to provide:
 * - Ring 0 kernel stack switching
 * - Interrupt Stack Table (IST) infrastructure
 * - Future userspace transition support
 *
 * References:
 * - Intel SDM Volume 3A:
 *   - Chapter 3: Protected-Mode Memory Management
 *   - Chapter 7: Task Management
 * - https://wiki.osdev.org/GDT_Tutorial
 * - https://wiki.osdev.org/Task_State_Segment
 */

#include "gdt.h"
#include "drivers/serial_port.h"


extern "C" void gdt_flush(struct GDTDescriptor* gdtr);
extern "C" void tss_flush();

// Entries: 0=Null, 1=KCode, 2=KData, 3=UData, 4=UCode, 5=TSS_Low, 6=TSS_High
static uint64_t gdt[7];
static struct GDTDescriptor gdtr;
static TSSEntry tss;

/**
 * @brief Primary kernel privilege-transition stack.
 *
 * This stack is used as the initial ring 0 stack during CPU
 * privilege transitions from user mode into kernel mode.
 *
 * The top of this stack is loaded into the Task State Segment
 * rsp0 field during GDT/TSS initialization.
 *
 * Stack growth direction:
 * - x86_64 stacks grow downward toward lower memory addresses
 * - The initial stack pointer is therefore the END of this array
 */
alignas(16) uint8_t kernel_stack[16384];

void SetGDTEntry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    SegmentDescriptor* desc = (SegmentDescriptor*)&gdt[index];

    desc->limit_low = limit & 0xFFFF;

    desc->base_low = base & 0xFFFF;
    desc->base_middle = (base >> 16) & 0xFF;
    desc->base_high = (base >> 24) & 0xFF;

    desc->access = access;

    // Combine 4 bits of limit-high and 4 bits of flags
    desc->granularity = ((limit >> 16) & 0x0F) | (flags & 0xF0);
}

void WriteTSS(int index, TSSEntry* tss)
{
    uint64_t base = (uint64_t)tss;
    uint32_t limit = sizeof(TSSEntry) - 1;
    
    TSSDescriptor* desc = (TSSDescriptor*)&gdt[index];

    desc->limit_low = limit & 0xFFFF;

    desc->base_low = base & 0xFFFF;
    desc->base_middle1 = (base >> 16) & 0xFF;
    desc->access = 0x89; // Present, Executable, Type 9 (Available 64-bit TSS)
    desc->granularity = ((limit >> 16) & 0x0F);
    desc->base_middle2 = (base >> 24) & 0xFF;

    desc->reserved = 0;
    desc->base_high = (base >> 32) & 0xFFFFFFFF;
}



void InitGDT()
{
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;

    SetGDTEntry(0, 0, 0, 0, 0);       // Null descriptor
    SetGDTEntry(1, 0, 0, 0x9A, 0x20); // Kernel Code: P=1, DPL=0, S=1, Type=0xA (Execute/Read Code) | Flags: L=1
    SetGDTEntry(2, 0, 0, 0x92, 0x00); // Kernel Data: P=1, DPL=0, S=1, Type=0x2 (Read/Write Data) | Flags cleared
    SetGDTEntry(3, 0, 0, 0xF2, 0x00); // User Data:   P=1, DPL=3, S=1, Type=0x2 (Read/Write Data) | Flags cleared
    SetGDTEntry(4, 0, 0, 0xFA, 0x20); // User Code:   P=1, DPL=3, S=1, Type=0xA | Flags: L=1

    memset(&tss, 0, sizeof(TSSEntry));
    tss.rsp0 = (uint64_t)kernel_stack + sizeof(kernel_stack);
    tss.iomap_base = sizeof(TSSEntry);
    WriteTSS(5, &tss);
    
    gdt_flush(&gdtr);
    tss_flush();

    uint64_t* gdt_raw = (uint64_t*)&gdt;

}
