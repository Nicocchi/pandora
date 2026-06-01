#include "vmm.h"
#include "common.h"

VirtualMemoryManager virtualMemoryManager;

// Extract the 9-bit index for each page-table level from a virtual address
static uint64_t PML4Index(uint64_t v)
{
    return (v >> 39) & 0x1FF;
}

static uint64_t PDPTIndex(uint64_t v)
{
    return (v >> 30) & 0x1FF;
}

static uint64_t PDIndex(uint64_t v)
{
    return (v >> 21) & 0x1FF;
}

static uint64_t PTIndex(uint64_t v)
{
    return (v >> 12) & 0x1FF;
}

// Allocate one zeroed page from the PMM and return its physical address
// Halts if the PMM is exhausted - this is unrecoverable during early boot
static uint64_t AllocZeroedPage()
{
    uint64_t phys = PMMAlloc(&g_pmm, 0);
    if (!phys) KernelPanic("VMM: PMM exhausted allocating page table\n");

    // Sanity check: phys must be within HHDM range
    if (phys + g_hhdm_offset < 0xFFFF800000000000ULL)
    {
        KernelPanic("VMM: bad physical address from PMM\n");
    }

    // Zero through HHDM before handing off
    uint64_t *virt = (uint64_t*)PhysToVirt(phys);
    for (int i = 0; i < 512; i++) virt[i] = 0;
    return phys;
}

// Physical -> vritual via HHDM
static uint64_t *PhysToTable(uint64_t phys)
{
    return (uint64_t*)PhysToVirt(phys);
}

// Walk/allocate the page-table hierarchy and return a pointer to the
// final PTE for virt. If alloc=true, missing intermediate tables are
// created; if alloc=false, nullptr is returned instead.
// Walk PML4 -> PDPT -> PD -> PT for `virt`.
// If alloc == true, missing intermediate tables are created via the PMM.
// If alloc == false, returns nullptr if any level is absent.
// Returns a pointer to the final PT entry (PTE) for `virt`.
uint64_t *WalkOrAlloc(AddressSpace *as, uint64_t virt, bool alloc)
{
    uint64_t *pml4 = PhysToTable(as->pml4_phys);

    // PML4 -> PDPT
    uint64_t &pml4e = pml4[PML4Index(virt)];
    if (!(pml4e & PTE_PRESENT))
    {
        if (!alloc) return nullptr;
        uint64_t pdpt_phys = AllocZeroedPage();
        // User address -> set USER bit so CPL3 can reach it
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (virt <= USER_SPACE_END) flags |= PTE_USER;
        pml4e = pdpt_phys | flags;
    }
    uint64_t *pdpt = PhysToTable(pml4e & PTE_ADDR_MASK);

    // PDPT -> PD
    uint64_t &pdpte = pdpt[PDPTIndex(virt)];
    if (!(pdpte & PTE_PRESENT))
    {
        if (!alloc) return nullptr;
        uint64_t pd_phys = AllocZeroedPage();
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (virt <= USER_SPACE_END) flags |= PTE_USER;
        pdpte = pd_phys | flags;
    }
    // Guard: Don't walk into a 1GiB huge page
    if (pdpte & PTE_HUGE) return nullptr;
    uint64_t *pd = PhysToTable(pdpte & PTE_ADDR_MASK);

    // PD -> PT
    uint64_t &pde = pd[PDIndex(virt)];
    if (!(pde & PTE_PRESENT))
    {
        if (!alloc) return nullptr;
        uint64_t pt_phys = AllocZeroedPage();
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (virt <= USER_SPACE_END) flags |= PTE_USER;
        pde = pt_phys | flags;
    }
    // Guard: Don't walk into a 2MiB huge page
    if (pde & PTE_HUGE) return nullptr;
    uint64_t *pt = PhysToTable(pde & PTE_ADDR_MASK);

    return &pt[PTIndex(virt)];
}

// Walk to the page-directory level and return a pointer to the PDE for `virt`.
static uint64_t *WalkToPD(AddressSpace *as, uint64_t virt, bool alloc)
{
    uint64_t *pml4 = PhysToTable(as->pml4_phys);

    uint64_t &pml4e = pml4[PML4Index(virt)];
    if (!(pml4e & PTE_PRESENT))
    {
        if (!alloc) return nullptr;
        uint64_t pdpt_phys = AllocZeroedPage();
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (virt <= USER_SPACE_END) flags |= PTE_USER;
        pml4e = pdpt_phys | flags;
    }
    uint64_t *pdpt = PhysToTable(pml4e & PTE_ADDR_MASK);

    uint64_t &pdpte = pdpt[PDPTIndex(virt)];
    if (!(pdpte & PTE_PRESENT))
    {
        if (!alloc) return nullptr;
        uint64_t pd_phys = AllocZeroedPage();
        uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
        if (virt <= USER_SPACE_END) flags |= PTE_USER;
        pdpte = pd_phys | flags;
    }
    if (pdpte & PTE_HUGE) return nullptr;

    uint64_t *pd = PhysToTable(pdpte & PTE_ADDR_MASK);
    return &pd[PDIndex(virt)];
}

static bool MapHuge2M(AddressSpace *as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    if ((virt | phys) & ((2ULL << 20) - 1)) return false;

    uint64_t *pde = WalkToPD(as, virt, true);
    if (!pde) return false;

    if (*pde & PTE_PRESENT)
    {
        if (!(*pde & PTE_HUGE)) return false;
    }

    *pde = (phys & PTE_ADDR_MASK) | flags | PTE_HUGE | PTE_PRESENT;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

bool AddressSpace::MapPage(uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pte = WalkOrAlloc(this, virt, true);
    if (!pte) return false;

    *pte = (phys & PTE_ADDR_MASK) | flags;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

void AddressSpace::UnmapPage(uint64_t virt)
{
    uint64_t *pte = WalkOrAlloc(this, virt, false);
    if (!pte) return;

    *pte = 0;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

bool AddressSpace::RemapPage(uint64_t virt, uint64_t flags)
{
    uint64_t *pte = WalkOrAlloc(this, virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) return false;

    *pte = (*pte & PTE_ADDR_MASK) | flags;
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

uint64_t AddressSpace::Translate(uint64_t virt) const
{
    // Cast away const - WalkOrAlloc doesn't modify when alloc=false
    uint64_t *pte = WalkOrAlloc(const_cast<AddressSpace*>(this), virt, false);
    if (!pte || !(*pte & PTE_PRESENT)) return 0;
    return (*pte & PTE_ADDR_MASK) | (virt & 0xFFF);
}

bool AddressSpace::MapRange(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags)
{
    static constexpr uint64_t HUGE_2M = 2ULL << 20;
    static constexpr uint64_t HUGE_2M_MASK = HUGE_2M - 1;

    uint64_t pos = phys;
    uint64_t v = virt;
    uint64_t end = phys + size;

    while (pos < end && (pos & (PAGE_SIZE - 1)))
    {
        if (!MapPage(v, pos, flags)) return false;
        pos += PAGE_SIZE;
        v += PAGE_SIZE;
    }

    while (pos + HUGE_2M <= end)
    {
        if (!MapHuge2M(this, v, pos, flags)) return false;
        pos += HUGE_2M;
        v += HUGE_2M;
    }

    while (pos < end)
    {
        if (!MapPage(v, pos, flags)) return false;
        pos += PAGE_SIZE;
        v += PAGE_SIZE;
    }

    return true;
}

void AddressSpace::UnmapRange(uint64_t virt, uint64_t size)
{
    uint64_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++)
    {
        UnmapPage(virt + i * PAGE_SIZE);
    }
}

void AddressSpace::Load() const
{
    asm volatile("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");
}

// Allocates a fresh PML4
// If kernel_as != nullptr (i.e. this is a user space), copies all 256
// upper-half PML4 entries from the kernel so kernel mappings are visible
// in every address space without duplication
bool AddressSpace::Init(const AddressSpace *kernel_as)
{
    pml4_phys = AllocZeroedPage();

    if (kernel_as)
    {
        // Share the upper 256 PML4 entries (kernel half, indices 256-511)
        uint64_t *dst = PhysToTable(pml4_phys);
        const uint64_t *src = PhysToTable(kernel_as->pml4_phys);
        for (int i = 256; i < 512; i++)
        {
            dst[i] = src[i];
        }
    }

    return true;
}

// Creates an independent copy of the user-space portion (PML4 entries 0-255)
// by deep-copying all present page tables down to the PT level.
// The kernel half (entries 256-511) is shared, not copied.
//
// NOTE: This copies page *table* structure only - the underlying physical
// frames mapped by the PTEs are NOT duplicated (no copy-on-write yet)
// That's intentional: COW belongs in the VMM/process layer above this.
bool AddressSpace::Fork(AddressSpace *dst) const
{
    dst->pml4_phys = AllocZeroedPage();

    const uint64_t *src_pml4 = PhysToTable(pml4_phys);
    uint64_t *dst_pml4 = PhysToTable(dst->pml4_phys);

    // Share kernel half
    for (int i = 256; i < 512; i++)
    {
        dst_pml4[i] = src_pml4[i];
    }

    // Deep-copy user half
    for (int pml4i = 0; pml4i < 256; pml4i++)
    {
        if (!(src_pml4[pml4i] & PTE_PRESENT)) continue;

        uint64_t dst_pdpt_phys = AllocZeroedPage();
        dst_pml4[pml4i] = dst_pdpt_phys | (src_pml4[pml4i] & ~PTE_ADDR_MASK);

        const uint64_t *src_pdpt = PhysToTable(src_pml4[pml4i] & PTE_ADDR_MASK);
        uint64_t *dst_pdpt = PhysToTable(dst_pdpt_phys);

        for (int pdpti = 0; pdpti < 512; pdpti++)
        {
            if (!(src_pdpt[pdpti] & PTE_PRESENT)) continue;
            if (src_pdpt[pdpti] & PTE_HUGE)
            {
                // 16GiB huge page - copy entry directly
                dst_pdpt[pdpti] = src_pdpt[pdpti];
                continue;
            }

            uint64_t dst_pd_phys = AllocZeroedPage();
            dst_pdpt[pdpti] = dst_pd_phys | (src_pdpt[pdpti] & ~PTE_ADDR_MASK);

            const uint64_t *src_pd = PhysToTable(src_pdpt[pdpti] & PTE_ADDR_MASK);
            uint64_t *dst_pd = PhysToTable(dst_pd_phys);

            for (int pdi = 0; pdi < 512; pdi++)
            {
                if (!(src_pd[pdi] & PTE_PRESENT)) continue;
                if (src_pd[pdi] & PTE_HUGE)
                {
                    // 2MiB huge page - copy entry directly
                    dst_pd[pdi] = src_pd[pdi];
                    continue;
                }

                uint64_t dst_pt_phys = AllocZeroedPage();
                dst_pd[pdi] = dst_pt_phys | (src_pd[pdi] & ~PTE_ADDR_MASK);

                const uint64_t *src_pt = PhysToTable(src_pd[pdi] & PTE_ADDR_MASK);
                uint64_t *dst_pt = PhysToTable(dst_pt_phys);

                // Copy all 512 PTEs (physical frames are shared, not duplicated)
                for (int pti = 0; pti < 512; pti++)
                {
                    dst_pt[pti] = src_pt[pti];
                }
            }
        }
    }

    return true;
}

void VirtualMemoryManager::Init(uint64_t kernel_phys, uint64_t kernel_virt, uint64_t kernel_size)
{
    // Enable NXE in EFER (preserve EDX — rdmsr returns 64 bits across EAX:EDX)
    uint32_t efer_lo, efer_hi;
    asm volatile(
        "mov $0xC0000080, %%ecx\n"
        "rdmsr\n"
        : "=a"(efer_lo), "=d"(efer_hi)
        :
        : "rcx"
    );
    efer_lo |= 0x800; // bit 11 = NXE
    asm volatile(
        "mov $0xC0000080, %%ecx\n"
        "wrmsr\n"
        :
        : "a"(efer_lo), "d"(efer_hi), "c"(0xC0000080U)
    );

    // Fresh kernel PML4
    kernel_space.Init(nullptr);

    // Inherit Limine's upper-half page-table subtree (HHDM + firmware mappings).
    // Re-walking the memmap with 4 KiB pages is slow and can exhaust the PMM on
    // machines with large RAM or MMIO windows (common under VirtualBox).
    uint64_t limine_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(limine_cr3));
    limine_cr3 &= PTE_ADDR_MASK;

    uint64_t *dst_pml4 = PhysToTable(kernel_space.pml4_phys);
    uint64_t *src_pml4 = PhysToTable(limine_cr3);
    for (int i = 256; i < 512; i++)
    {
        dst_pml4[i] = src_pml4[i];
    }

    // Map kernel image
    uint64_t k_pages = (kernel_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = 0; i < k_pages; i++)
    {
        uint64_t phys = kernel_phys + i * PAGE_SIZE;
        uint64_t virt = kernel_virt + i * PAGE_SIZE;
        kernel_space.MapPage(virt, phys, VMM_FLAGS_KERNEL_RW);
    }

    // Switch off from Limine page tables
    kernel_space.Load();

    kprintf("VMM Init finished\n");
}

bool VirtualMemoryManager::MapPage(uint64_t virt, uint64_t phys, uint64_t flags)
{
    return kernel_space.MapPage(virt, phys, flags);
}
void VirtualMemoryManager::UnmapPage(uint64_t virt)
{
    kernel_space.UnmapPage(virt);
}
bool VirtualMemoryManager::RemapPage(uint64_t virt, uint64_t flags)
{
    return kernel_space.RemapPage(virt, flags);
}
uint64_t VirtualMemoryManager::Translate(uint64_t virt) const
{
    return kernel_space.Translate(virt);
}

bool VirtualMemoryManager::MapRange(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags)
{
    return kernel_space.MapRange(virt, phys, size, flags);
}
void VirtualMemoryManager::UnmapRange(uint64_t virt, uint64_t size)
{
    kernel_space.UnmapRange(virt, size);
}