#pragma once

#include <stdint.h>
#include <stddef.h>
#include "pmm.h"

// ============================================================================
//  Virtual address space layout (x86-64, 48-bit canonical)
//
//   0x0000000000000000 – 0x00007FFFFFFFFFFF   user space   (128 TiB)
//   0xFFFF800000000000 – 0xFFFFFFFFFFFFFFFF   kernel space (128 TiB)
//
//  Kernel regions:
//   0xFFFF800000000000  HHDM base (Limine, all physical RAM mapped here)
//   0xFFFFFFFF80000000  kernel image base
// ============================================================================
static constexpr uint64_t USER_SPACE_END = 0x00007FFFFFFFFFFFULL;
static constexpr uint64_t KERNEL_SPACE_BASE = 0xFFFF800000000000ULL;

// Page table entry flags (x86-64 PTE bits)
static constexpr uint64_t PTE_PRESENT = (1ULL << 0);
static constexpr uint64_t PTE_WRITABLE = (1ULL << 1);
static constexpr uint64_t PTE_USER = (1ULL << 2);    // Accessible from CPL3
static constexpr uint64_t PTE_PWT = (1ULL << 3);     // Write-through
static constexpr uint64_t PTE_PCD = (1ULL << 4);     // Cache disable (use for MMIO)
static constexpr uint64_t PTE_ACCESSED = (1ULL << 5);
static constexpr uint64_t PTE_DIRTY = (1ULL << 6);
static constexpr uint64_t PTE_HUGE = (1ULL << 7);    // 2MiB / 1GiB page
static constexpr uint64_t PTE_GLOBAL = (1ULL << 8);  // Not flushed on CR3 reload
static constexpr uint64_t PTE_NX = (1ULL << 63);     // No-execute (requires EFER.NXE)

static constexpr uint64_t VMM_FLAGS_KERNEL_RW = PTE_PRESENT | PTE_WRITABLE | PTE_GLOBAL;
static constexpr uint64_t VMM_FLAGS_KERNEL_RO = PTE_PRESENT | PTE_GLOBAL | PTE_NX;
static constexpr uint64_t VMM_FLAGS_KERNEL_EX = PTE_PRESENT | PTE_GLOBAL;
static constexpr uint64_t VMM_FLAGS_USER_RW = PTE_PRESENT | PTE_WRITABLE | PTE_USER;
static constexpr uint64_t VMM_FLAGS_USER_RO = PTE_PRESENT | PTE_USER | PTE_NX;
static constexpr uint64_t VMM_FLAGS_MMIO = PTE_PRESENT | PTE_WRITABLE | PTE_PCD | PTE_PWT | PTE_GLOBAL;

extern BuddyAllocator g_pmm;

// Physical address mask (strip flag bits from a PTE)
static constexpr uint64_t PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;

/**
 * @brief Wraps a single 4KiB page of 512 x uint64_t PTEs
 * 
 */
struct PageTable
{
    uint64_t entries[512];
} __attribute__((aligned(4096)));

struct AddressSpace
{
    uint64_t pml4_phys; /**< Physical address of PML4 table */

    // Map [virt, virt + PAGE_SIZE] -> phys with the given flags
    // Allocates intermediate page tables from the PMM as needed
    // Returns false if the PMM is exhausted
    bool MapPage(uint64_t virt, uint64_t phys, uint64_t flags);

    // Unmap a single page. Does NOT free the physical frame (caller owns it)
    void UnmapPage(uint64_t virt);

    // Remap an existing mapping with new flags (address unchanged)
    bool RemapPage(uint64_t virt, uint64_t flags);

    // Translate a virtual address to its physical address
    // Retursn 0 if not mapped
    uint64_t Translate(uint64_t virt) const;

    // Map a contiguous range. Calls MapPage for each page.
    bool MapRange(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

    // Unmap a contiguous range
    void UnmapRange(uint64_t virt, uint64_t size);

    // Load this address space into CR3
    void Load() const;

    // Initialise a fresh AddressSpace
    // kernel_as != nullptr -> copy kernel upper-half entries into this space
    // kernel_as == nullptr -> this IS the kernel address space
    bool Init(const AddressSpace *kernel_as);

    // Fork the user-space portion of this address space into dst
    // (kernel entries are shared, user entries are deep-copied).
    // Returns false on PMM exhaustion
    bool Fork(AddressSpace *dst) const;

};

struct VirtualMemoryManager
{
    AddressSpace kernel_space;

    // Initialize using the Limine-provided page tables as a reference
    // Builds a fresh kernel PML4, maps the kernel image + full HHDM,
    // then switches CR3 to the new tables
    // Enables NXE bit in EFER so PTE_NX is honoured.
    // Builds a fresh kernel PML4.
    // Maps the full HHDM window (all physical RAM) as kernel RW + NX.
    // Maps the kernel image (text RX, rodata RO, data RW).
    // Switches CR3 to the new tables - Limine's tables are abandoned.
    void Init(uint64_t hhdm_base, uint64_t hhdm_size, uint64_t kernel_phys,
                uint64_t kernel_virt, uint64_t kernel_size);
    
    // Global kernel map/unmap (delegates to kernel_space)
    bool MapPage(uint64_t virt, uint64_t phys, uint64_t flags);
    void UnmapPage(uint64_t virt);
    bool RemapPage(uint64_t virt, uint64_t flags);
    uint64_t Translate(uint64_t virt) const;

    bool MapRange(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
    void UnmapRange(uint64_t virt, uint64_t size);
};

extern VirtualMemoryManager virtualMemoryManager;

inline bool VMMMap(VirtualMemoryManager* vmm, uint64_t v, uint64_t p, uint64_t f)
{
    return vmm->MapPage(v, p, f);
}

inline void VMMUnmap(VirtualMemoryManager* vmm, uint64_t v)
{
    vmm->UnmapPage(v);
}

inline uint64_t VMMTranslate(VirtualMemoryManager* vmm, uint64_t v)
{
    return vmm->Translate(v);
}