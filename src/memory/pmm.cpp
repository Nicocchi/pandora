/**
 * @file        pmm.cpp
 * @brief       Physical Memory Manager (Buddy Allocator) implementation
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 31, 2026
 *
 * @details     Implements the core logic for the binary buddy allocator. This file
 *              contains private page-frame-number (PFN) helpers, bit-togglign routines
 *              for tracking buddy pairings, region feeding algorithms, and the primary
 *              allocation and coalescing code blocks.
 */

#include "pmm.h"
#include "common.h"

/** 
 * @brief Global singleton instance of the Physical Memory Manager.
 * @note  Initialized by the kernel early bootstrap sequence via BuddyAllocator::Init.
 */
BuddyAllocator g_pmm;

void FreeList::init()
{
    head.next = &head;
    head.prev = &head;
    count = 0;
}

void FreeList::push(BuddyBlock *blk)
{
    blk->next = head.next;
    blk->prev = &head;
    head.next->prev = blk;
    head.next = blk;
    ++count;
}

BuddyBlock *FreeList::pop()
{
    if (empty()) return nullptr;
    BuddyBlock *blk = head.next;
    remove(blk);
    return blk;
}

void FreeList::remove(BuddyBlock *blk)
{
    blk->prev->next = blk->next;
    blk->next->prev = blk->prev;
    blk->next = blk->prev = nullptr;
    --count;
}

bool FreeList::contains(BuddyBlock *blk) const
{
    for (BuddyBlock *n = head.next; n != &head; n = n->next)
    {
        if (n == blk) return true;
    }
    return false;
}

/**
 * @brief  Internal helper to convert the raw physical bitmap pointer to a virtual address.
 * @param  ba Pointer to the active allocator state context.
 * @return Virtual pointer to the base tracking bitmap memory zone.
 */
static inline uint8_t* BitmapVirt(BuddyAllocator* ba)
{
    return (uint8_t*)PhysToVirt((uint64_t)ba->bitmap);
}

/**
 * @brief  Toggles the allocation state bit shared between two adjacent buddy pages.
 * @details Uses an XOR bit-mask toggle strategy where a value of @c 0 indicates both buddies 
 *          share an identical state (either both allocated or both free), enabling coalescing.
 * @param  ba    Pointer to the active allocator state context.
 * @param  pfn   Page frame number index relative to the allocator base layout.
 * @param  order Size order layer scaling factor index.
 */
void BuddyBitFlip(BuddyAllocator* ba, uint64_t pfn, uint8_t order)
{
    uint64_t idx = pfn >> (order + 1);
    BitmapVirt(ba)[idx >> 3] ^= (1u << (idx & 7));
}

/**
 * @brief  Reads the current bit state shared between a buddy block pair.
 * @param  ba    Pointer to the active allocator state context.
 * @param  pfn   Page frame number index relative to the allocator base layout.
 * @param  order Size order layer scaling factor index.
 * @return @c true if the buddy tracking bit status is active (set to 1); otherwise, @c false.
 */
bool BuddyBitTest(BuddyAllocator* ba, uint64_t pfn, uint8_t order)
{
    uint64_t idx = pfn >> (order + 1);
    return (BitmapVirt(ba)[idx >> 3] >> (idx & 7)) & 1;
}

/**
 * @brief  Translates a system raw physical memory address into a relative page frame index.
 * @param  ba   Pointer to the active allocator state context.
 * @param  addr Target raw physical base memory address to convert.
 * @return Evaluated absolute page frame number tracking index.
 */
uint64_t AddrToPFN(BuddyAllocator* ba, uint64_t addr)
{
    return (addr - ba->base) >> PAGE_SHIFT;
}

/**
 * @brief  Translates a relative page frame index back into a system raw physical address.
 * @param  ba  Pointer to the active allocator state context.
 * @param  pfn Relative page frame number index to reconstruct.
 * @return Reconstructed raw physical base memory address.
 */
uint64_t PFNToAddr(BuddyAllocator* ba, uint64_t pfn)
{
    return ba->base + (pfn << PAGE_SHIFT);
}

/**
 * @brief  Breaks down and distributes an arbitrary physical memory segment into the free lists.
 * @details Iteratively discovers the largest naturally aligned binary block sizes that fit
 *          within the segment boundaries, initializing individual list blocks dynamically inside
 *          the free pools.
 * @param  ba   Pointer to the active allocator state context.
 * @param  base Starting raw physical address of the discovered memory segment.
 * @param  len  Total cumulative span distance size of the segment region in bytes.
 */
void AddRegion(BuddyAllocator* ba, uint64_t base, uint64_t len)
{
    // Align base up to page size
    uintptr_t aligned = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (aligned >= base + len) return;
    len -= (aligned - base);
    base = aligned;

    while (len >= PAGE_SIZE)
    {
        // Find the largest order block that:
        //  - fits within the remaining length, and
        //  - is naturally aligned at this address
        uint8_t order = MAX_ORDER - 1;

        while (order > 0)
        {
            uint64_t block_size = (uint64_t)PAGE_SIZE << order;
            if (block_size <= len && (base & (block_size - 1)) == 0) break;
            -- order;
        }

        uint64_t block_size = (uint64_t)PAGE_SIZE << order;

        // Write the free-list node directly into the block - free memory
        // BuddyBlock *blk = (BuddyBlock*)base;
        BuddyBlock *blk = (BuddyBlock*)PhysToVirt(base);
        ba->lists[order].push(blk);

        // Flip the buddy-pair bit to mark this block as free
        BuddyBitFlip(ba, AddrToPFN(ba, base), order);

        ba->free_pages += (1u << order);
        base += block_size;
        len -= block_size;
    }
}

void BuddyAllocator::Init(limine_memmap_response *mmap)
{
    // Determine the full usable physical address range
    uintptr_t phys_min = UINT64_MAX;
    uintptr_t phys_max = 0;

    for (uint64_t i = 0; i < mmap->entry_count; ++i)
    {
        limine_memmap_entry *e = mmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uintptr_t start = e->base;
        uintptr_t end = e->base + e->length;

        if (start < phys_min) phys_min = start;
        if (end > phys_max) phys_max = end;
    }

    // Align base down, top up to page boundaries
    base = phys_min & ~(PAGE_SIZE - 1);
    uintptr_t top = (phys_max + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    free_pages = 0;

    total_pages = 0;
    for (uint64_t i = 0; i < mmap->entry_count; ++i)
    {
        limine_memmap_entry *e = mmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        total_pages += e->length >> PAGE_SHIFT;
    }

    // Carve bitmap storage from the first large-enough usable region
    // Bitmap size: (total_pages / 2) bitgs, one per buddy pair, rounded up
    // to the next page so the first managed page is page-aligned
    bitmap_size = ((total_pages / 2) + 7) / 8;                          // bytes needed
    bitmap_size = (bitmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);     // round up

    bitmap = nullptr;

    for (uint64_t i = 0; i < mmap->entry_count; ++i)
    {
        limine_memmap_entry *e = mmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->length >= bitmap_size)
        {
            bitmap = (uint8_t*)(e->base);
            break;
        }
    }

    // Halt if couldn't place the bitmap
    if (!bitmap)
    {
        KernelPanic("Could not place bitmap\n");
    }

    uint8_t *bitmap_virt = (uint8_t*)PhysToVirt((uint64_t)bitmap);
    for (uint64_t b = 0; b < bitmap_size; ++b)
    {
        bitmap_virt[b] = 0;
    }

    // Initialize free lists
    for (uint8_t i = 0; i < MAX_ORDER; ++i)
    {
        lists[i].init();
    }

    // Feed usable regions into the allocator
    for (uint64_t i = 0; i < mmap->entry_count; ++i)
    {
        limine_memmap_entry *e = mmap->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uintptr_t base = e->base;
        uint64_t len = e->length;

        // Skip the pages consumed for the bitmap
        if ((uintptr_t)bitmap >= base && (uintptr_t)bitmap < base + len)
        {
            uintptr_t bitmap_end = (uintptr_t)bitmap + bitmap_size;
            if (bitmap_end >= base + len) continue; // Whole region consumed
            len = (base + len) - bitmap_end;
            base = bitmap_end;
        }

        AddRegion(this, base, len);
    }
}

uintptr_t BuddyAllocator::Alloc(uint8_t order)
{
    if (order >= MAX_ORDER) return 0;

    // Find the smallest available order >= requested order
    uint8_t found = MAX_ORDER;
    for (uint8_t i = order; i < MAX_ORDER; ++i)
    {
        if (!lists[i].empty()) {
            found = i;
            break;
        }
    }

    if (found == MAX_ORDER) return 0; // OOM

    // Pop the block from its free list
    BuddyBlock *blk = lists[found].pop();
    uintptr_t addr = VirtToPhys(blk);

    // Mark as allocated in the buddy-pair bitmap
    BuddyBitFlip(this, AddrToPFN(this, addr), found);

    // Split down to the requested order, pushing the upper halves back
    while (found > order)
    {
        --found;
        uint64_t half = (uint64_t)PAGE_SIZE << found;
        uintptr_t split_off = addr + half;
        BuddyBlock *buddy_blk = (BuddyBlock*)PhysToVirt(split_off);

        lists[found].push(buddy_blk);
        BuddyBitFlip(this, AddrToPFN(this, split_off), found);
    }

    free_pages -= (1u << order);
    return addr;
}

/**
 * @brief  Computes the sister buddy address block in the binary allocation pairing trees.
 * @details Performs a target bit toggle calculations using XOR operations against size scaling factors:
 *          \f$\text{Address} \oplus (\text{PAGE\_SIZE} \times 2^{\text{order}})\f$.
 * @param  addr  Starting raw physical base tracking reference address.
 * @param  order Block size exponent calculation index matching current tree layers.
 * @return Calculated sister buddy target physical base memory address.
 */
uint64_t BuddyAddr(uint64_t addr, uint8_t order)
{
    return addr ^ ((uint64_t)PAGE_SIZE << order);
}

void BuddyAllocator::Free(uintptr_t addr, uint8_t order)
{
    if (!addr || order >= MAX_ORDER) return;

    // Align address to block size (guard against caller mistakes)
    addr &= ~(((uintptr_t)PAGE_SIZE << order) - 1);

    free_pages += (1u << order);

    while (order < MAX_ORDER -1)
    {
        uint64_t pfn = AddrToPFN(this, addr);
        uintptr_t buddy_addr = BuddyAddr(addr, order);

        // BuddyBitFlip before testing: if the bit becomes 0, both buddies
        // are now free -> merge. If it becomes 1, the buddy is still allocated
        BuddyBitFlip(this, pfn, order);
        if (BuddyBitTest(this, pfn, order))
        {
            // Buddy is still in use - stop coalescing
            break;
        }

        // Remove buddy from its free list and merge
        // BuddyBlock *buddy_blk = (BuddyBlock*)buddy_addr;
        BuddyBlock *buddy_blk = (BuddyBlock*)PhysToVirt(buddy_addr);
        lists[order].remove(buddy_blk);

        // Merged block starts at the lower address
        if (buddy_addr < addr) addr = buddy_addr;
        ++order;
    }

    // Push the (possibly coalesced) block onto its free list
    // BuddyBlock *blk = (BuddyBlock*)addr;
    BuddyBlock *blk = (BuddyBlock*)PhysToVirt(addr);
    lists[order].push(blk);
}


// Bitmap helpers (one bit per page; set = block at this pfn is the 
// *merged* / free representative at its current order)
// void BitmapSet(BuddyAllocator* ba, uint64_t pfn)
// {
//     BitmapVirt(ba)[pfn >> 3] |=  (1u << (pfn & 7));
// }
// void BitmapClear(BuddyAllocator* ba, uint64_t pfn)
// {
//     BitmapVirt(ba)[pfn >> 3] &= ~(1u << (pfn & 7));
// }
// bool BitmapTest(BuddyAllocator* ba, uint64_t pfn)
// {
//     return (BitmapVirt(ba)[pfn >> 3] >> (pfn & 7)) & 1;
// }