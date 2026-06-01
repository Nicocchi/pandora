/**
 * @file        apic.cpp
 * @brief       Advanced Programmable Interrupt Controller (APIC) implementations.
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 29, 2026
 *
 * @details
 */

#include "apic.h"
#include "common.h"
#include "lib/string.h"
#include "boot/limine_vga.h"
#include "memory/vmm.h"



__attribute__((used, section(".limine_requests")))
volatile limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0
};


// Key Local APIC registers (offsets from base)
#define LAPIC_ID          0x020
#define LAPIC_EOI         0x0B0  // Write 0 to signal EOI
#define LAPIC_SPURIOUS    0x0F0  // Spurious interrupt vector
#define LAPIC_TIMER_LVT   0x320
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_COUNT 0x390
#define LAPIC_TIMER_DIV   0x3E0

static uint64_t lapic_base = 0;


static uint32_t LAPICRead(uint32_t offset)
{
    return *((volatile uint32_t*)(lapic_base + offset));
}

static void LAPICWrite(uint32_t offset, uint32_t value)
{
    *((volatile uint32_t*)(lapic_base + offset)) = value;
}

void InitLAPIC()
{
    // Enable local APIC by setting bit 8 of spurious vector register
    // Vector 0xFF is conventional for spurious interrupts
    LAPICWrite(LAPIC_SPURIOUS, LAPICRead(LAPIC_SPURIOUS) | (1 << 8) | 0xFF);
}

void LAPICEoi()
{
    LAPICWrite(LAPIC_EOI, 0);
}

#define IOAPIC_REGSEL  0x00
#define IOAPIC_IOWIN   0x10
#define IOAPIC_REDTBL  0x10  // Redirection table base (2 regs per entry)

static uint64_t ioapic_base = 0;

static uint32_t IOAPICRead(uint8_t reg)
{
    *((volatile uint32_t*)(ioapic_base + IOAPIC_REGSEL)) = reg;
    return *((volatile uint32_t*)(ioapic_base + IOAPIC_IOWIN));
}

static void IOAPICWrite(uint8_t reg, uint32_t value)
{
    *((volatile uint32_t*)(ioapic_base + IOAPIC_REGSEL)) = reg;
    *((volatile uint32_t*)(ioapic_base + IOAPIC_IOWIN)) = value;
}

// Each redirection entry is 64 bits split across two 32-bit registers
void IOAPICSetRedirect(uint8_t irq, uint8_t vector, uint8_t apic_id, bool mask)
{
    uint32_t low = vector | (mask ? (1 << 16) : 0);
    uint32_t high = ((uint32_t)apic_id << 24);

    IOAPICWrite(IOAPIC_REDTBL + irq * 2, low);
    IOAPICWrite(IOAPIC_REDTBL + irq * 2+1, high);
}

static ISOEntry isos[16];
static uint8_t iso_count = 0;

static void IOAPICRouteIRQ(uint8_t irq, uint8_t vector, uint8_t apic_id)
{
    // Check for ISO override
    uint32_t gsi = irq;
    for (uint8_t i = 0; i < iso_count; i++)
    {
        if (isos[i].irq == irq)
        {
            gsi = isos[i].gsi;
            break;
        }
    }

    IOAPICSetRedirect(gsi, vector, apic_id, false);
}

void InitIOAPIC()
{
    IOAPICRouteIRQ(0, 32, 0);
    IOAPICRouteIRQ(1, 33, 0);
}

__attribute__((used, section(".limine_requests")))
volatile struct limine_executable_address_request exe_addr_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0
};

static uint64_t kernel_virt_base = 0;
static uint64_t kernel_phys_base = 0;

alignas(4096) static uint8_t mmio_page_tables[4096 * 4]; // 4 pages for PT allocation
static uint64_t mmio_pt_alloc_offset = 0;
// static uint64_t hhdm_offset = 0;

// static uint64_t VirtToPhys(uint64_t virt)
// {
//     // For addresses in the kernel image
//     if (virt >= kernel_virt_base)
//         return virt - kernel_virt_base + kernel_phys_base;
    
//     // For addresses in the HHDM region
//     return virt - hhdm_offset;
// }
static uint64_t APICVirtToPhys(uint64_t virt)
{
    // For addresses in the kernel image
    if (virt >= kernel_virt_base)
        return virt - kernel_virt_base + kernel_phys_base;
    
    // For addresses in the HHDM region
    return virt - g_hhdm_offset;
}

static uint64_t AllocPageTable()
{
    if (mmio_pt_alloc_offset + 4096 > sizeof(mmio_page_tables))
    {
        KernelPanic("MapMMIO: out of page table space\n");
    }

    uint64_t virt = (uint64_t)(mmio_page_tables + mmio_pt_alloc_offset);
    mmio_pt_alloc_offset += 4096;
    memset((void*)virt, 0, 4096);
    return virt;
}

// static void MapMMIO(uint64_t phys, uint64_t virt)
// {
//     uint64_t cr3;
//     asm volatile("mov %%cr3, %0" : "=r"(cr3));

//     uint64_t* pml4 = (uint64_t*)((cr3 & ~0xFFFULL) + g_hhdm_offset);

//     uint64_t pml4_idx = (virt >> 39) & 0x1FF;
//     uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
//     uint64_t pd_idx   = (virt >> 21) & 0x1FF;
//     uint64_t pt_idx   = (virt >> 12) & 0x1FF;

//     if (!(pml4[pml4_idx] & 1))
//     {
//         uint64_t pt_virt = AllocPageTable();
//         pml4[pml4_idx] = APICVirtToPhys(pt_virt) | 0x03;
//     }
//     uint64_t* pdpt = (uint64_t*)((pml4[pml4_idx] & ~0xFFFULL) + g_hhdm_offset);

//     if (!(pdpt[pdpt_idx] & 1))
//     {
//         uint64_t pt_virt = AllocPageTable();
//         pdpt[pdpt_idx] = APICVirtToPhys(pt_virt) | 0x03;
//     }
//     uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & ~0xFFFULL) + g_hhdm_offset);

//     if (pd[pd_idx] & (1 << 7))
//     {
//         // Split 2MB huge page into 512 x 4KB pages
//         uint64_t huge_phys  = pd[pd_idx] & ~0x1FFFFFULL;
//         uint64_t huge_flags = pd[pd_idx] & 0xFFF & ~(1 << 7);

//         uint64_t new_pt_virt = AllocPageTable();
//         uint64_t* new_pt = (uint64_t*)new_pt_virt;

//         for (int i = 0; i < 512; i++)
//             new_pt[i] = (huge_phys + i * 4096) | huge_flags;

//         pd[pd_idx] = APICVirtToPhys(new_pt_virt) | 0x03;

//         uint64_t base = virt & ~0x1FFFFFULL;
//         for (int i = 0; i < 512; i++)
//             asm volatile("invlpg (%0)" : : "r"(base + i * 4096) : "memory");
//     }
//     else if (!(pd[pd_idx] & 1))
//     {
//         uint64_t pt_virt = AllocPageTable();
//         pd[pd_idx] = APICVirtToPhys(pt_virt) | 0x03;
//     }

//     uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFFULL) + g_hhdm_offset);
//     pt[pt_idx] = phys | 0x13;

//     asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
// }


void InitAPIC()
{

    // Get RSDP from Limine
    if (rsdp_request.response == nullptr)
    {
        KernelPanic("RSDP not found\n");
    }

    // if (hhdm_request.response == nullptr)
    // {
    //     KernelPanic("HHDM not found\n");
    // }
    // hhdm_offset = hhdm_request.response->offset;

    void* rsdp_addr = rsdp_request.response->address;

    RSDP2* rsdp = (RSDP2*)rsdp_addr;

    // Walk XSDT if available, otherwise RSDT
    bool use_xsdt = (rsdp->revision >= 2 && rsdp->xsdt_address != 0);

    ACPISDTHeader* madt = nullptr;

    if (use_xsdt)
    {
        ACPISDTHeader* xsdt = (ACPISDTHeader*)(rsdp->xsdt_address + g_hhdm_offset);

        // Print raw bytes to verify
        uint8_t* raw = (uint8_t*)xsdt;

        uint64_t entries = (xsdt->length - sizeof(ACPISDTHeader)) / 8;
        uint64_t* table_ptrs = (uint64_t*)((uint8_t*)xsdt + sizeof(ACPISDTHeader));

        for (uint64_t i = 0; i < entries; i++)
        {
            ACPISDTHeader* header = (ACPISDTHeader*)(table_ptrs[i] + g_hhdm_offset);
            if (memcmp(header->signature, "APIC", 4) == 0)
            {
                madt = header;
                break;
            }
        }
    }
    else
    {
        ACPISDTHeader* rsdt = (ACPISDTHeader*)(uint64_t)(rsdp->rsdt_address + g_hhdm_offset);
        uint64_t entries = (rsdt->length - sizeof(ACPISDTHeader)) / 4;
        uint32_t* table_ptrs = (uint32_t*)((uint8_t*)rsdt + sizeof(ACPISDTHeader));

        for (uint64_t i = 0; i < entries; i++)
        {
            ACPISDTHeader* header = (ACPISDTHeader*)(table_ptrs[i] + g_hhdm_offset);
            if (memcmp(header->signature, "APIC", 4) == 0)
            {
                madt = header;
                break;
            }
        }
    }

    if (madt == nullptr)
    {
        KernelPanic("MADT not found\n");
    }

    // Parse MADT
    MADTHeader* madt_header = (MADTHeader*)madt;
    lapic_base = madt_header->local_apic_addr + g_hhdm_offset;

    kprintf("[OK] MADT found\n");
    
    uint8_t* entry_ptr = (uint8_t*)madt + sizeof(MADTHeader);
    uint8_t* madt_end = (uint8_t*)madt + madt_header->length;
    uint64_t ioapic_phys = 0;

    while (entry_ptr < madt_end)
    {
        MADTEntry* entry = (MADTEntry*)entry_ptr;

        switch(entry->type)
        {
            case MADT_LOCAL_APIC:
            {
                MADTLocalAPIC* local = (MADTLocalAPIC*)entry;
                if (local->flags & 1)
                {
                    kprintf("[OK] CPU found\n");
                }
            } break;

            case MADT_IO_APIC:
            {
                MADTIOApic* io = (MADTIOApic*)entry;
                ioapic_phys = io->io_apic_addr;
                ioapic_base = ioapic_phys + g_hhdm_offset;
                // ioapic_base = io->io_apic_addr + hhdm_offset;
                kprintf("[OK] I/O APIC found\n");
            } break;

            case MADT_ISO:
            {
                MADTIso* iso = (MADTIso*)entry;
                if (iso_count < 16)
                {
                    isos[iso_count++] = { iso->irq, iso->gsi, iso->flags };
                }
            } break;
        }

        entry_ptr += entry->length;
    }

    uint64_t lapic_phys = madt_header->local_apic_addr;
    if (ioapic_phys == 0)
    {
        KernelPanic("I/O APIC not found\n");
    }

    if (exe_addr_request.response == nullptr)
        KernelPanic("exe_addr_request not found\n");
    
    kernel_virt_base = exe_addr_request.response->virtual_base;
    kernel_phys_base = exe_addr_request.response->physical_base;

    // MapMMIO(lapic_phys, lapic_phys + g_hhdm_offset);
    // MapMMIO(ioapic_phys, ioapic_phys + g_hhdm_offset);

    virtualMemoryManager.MapPage(lapic_phys + g_hhdm_offset, lapic_phys, VMM_FLAGS_MMIO);
    virtualMemoryManager.MapPage(ioapic_phys + g_hhdm_offset, ioapic_phys, VMM_FLAGS_MMIO);

}