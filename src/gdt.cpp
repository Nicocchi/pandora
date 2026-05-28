#include "gdt.h"


extern "C" void gdt_flush(struct GDTDescriptor* gdtr);

static struct SegmentDescriptor gdt[5];
static struct GDTDescriptor gdtr;

void SetGDTGate(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    gdt[index].limit_low = limit & 0xFFFF;

    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_middle = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;

    gdt[index].access = access;

    gdt[index].granularity = ((limit >> 16) & 0x0F);

    gdt[index].granularity |= (flags & 0xF0);
}

void InitGDT()
{
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (uint64_t)&gdt;

    SetGDTGate(0, 0, 0, 0, 0);       // Null descriptor
    SetGDTGate(1, 0, 0, 0x9A, 0x20); // Kernel code segment
    SetGDTGate(2, 0, 0, 0x92, 0x00); // Kernel data segment
    SetGDTGate(3, 0, 0, 0xF2, 0x00); // User data segment
    SetGDTGate(4, 0, 0, 0xFA, 0x20); // User code segment

    gdt_flush(&gdtr);
}
