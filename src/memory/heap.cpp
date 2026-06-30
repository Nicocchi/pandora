#include "heap.h"
#include "pmm.h"
#include "common.h"
#include "drivers/serial_port.h"

KernelHeap kernelHeap;

// kmalloc must not be preempted mid-allocation (slab freelist / VMM walk).
static int heap_lock_depth = 0;
static uint64_t heap_lock_flags = 0;

static void HeapLock()
{
    if (heap_lock_depth++ == 0)
    {
        asm volatile("pushfq; pop %0; cli" : "=r"(heap_lock_flags) : : "memory");
    }
}

static void HeapUnlock()
{
    if (heap_lock_depth > 0 && --heap_lock_depth == 0)
    {
        asm volatile("push %0; popfq" :: "r"(heap_lock_flags) : "memory", "cc");
    }
}

static void kmemset_zero(void *ptr, size_t n)
{
    uint8_t *p = (uint8_t*)ptr;
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static void kmemcpy_bytes(void *dst, const void *src, size_t n)
{
    const uint8_t *s = (const uint8_t*)src;
    uint8_t *d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static size_t CacheIndexFor(size_t size)
{
    for (size_t i = 0; i < SLAB_CLASS_COUNT; i++)
    {
        if (size <= SLAB_SIZES[i]) return i;
    }
    return SLAB_CLASS_COUNT;
}

uint64_t KernelHeap::MapHeapPages(uint32_t pages, uint64_t *phys_out, uint8_t pmm_order)
{
    // Sanity: don't overrun the heap VA ceiling
    uint64_t virt = heap_virt_next;
    if (virt + (uint64_t)pages * PAGE_SIZE > HEAP_END)
    {
        PanicContext ctx = {};
        ctx.message = "KernelHeap:: virtual address space exhausted";
        KernelPanic(ctx);
        return 0;
    }

    // Allocate physical pages from PMM
    uint64_t phys = PMMAlloc(&g_pmm, pmm_order);
    if (!phys) return 0; // OOM - caller handles gracefully

    // Map each page into the VMM
    for (uint32_t i = 0; i < pages; i++)
    {
        bool ok = virtualMemoryManager.MapPage(
            virt + (uint64_t)i * PAGE_SIZE,
            phys + (uint64_t)i * PAGE_SIZE,
            VMM_FLAGS_KERNEL_RW
        );
        if (!ok)
        {
            // Unmap what we alredy mapped, free physical pages, fail
            for (uint32_t j = 0; j < i; j++)
            {
                virtualMemoryManager.UnmapPage(virt + (uint64_t)j * PAGE_SIZE);
            }
            PMMFree(&g_pmm, phys, pmm_order);
            return 0;
        }
    }

    heap_virt_next += (uint64_t)pages * PAGE_SIZE;
    total_mapped += (uint64_t)pages * PAGE_SIZE;

    if (phys_out) *phys_out = phys;
    return virt;
}

void KernelHeap::Init()
{
    heap_virt_next = HEAP_BASE;
    total_allocated = 0;
    total_mapped = 0;

    for (size_t i = 0; i < SLAB_CLASS_COUNT; i++)
    {
        caches[i].Init(SLAB_SIZES[i]);
    }
}

void *KernelHeap::Alloc(size_t size)
{
    if (size == 0) return nullptr;

    HeapLock();

    void *result = nullptr;
    size_t idx = CacheIndexFor(size);

    if (idx < SLAB_CLASS_COUNT)
    {
        result = caches[idx].Alloc();
        if (result) total_allocated += SLAB_SIZES[idx];
    }
    else
    {
        size_t total_size = sizeof(LargeAllocHeader) + size;
        uint32_t pages = (uint32_t)((total_size + PAGE_SIZE - 1) / PAGE_SIZE);

        uint8_t order = 0;
        while ((1u << order) < pages) order++;

        if (order < MAX_ORDER)
        {
            uint64_t phys = 0;
            uint64_t virt = MapHeapPages(1u << order, &phys, order);
            if (virt)
            {
                LargeAllocHeader *hdr = (LargeAllocHeader*)virt;
                hdr->magic = LARGE_ALLOC_MAGIC;
                hdr->virt_base = virt;
                hdr->phys_base = phys;
                hdr->requested_size = size;
                hdr->page_count = 1u << order;
                hdr->order = order;
                total_allocated += size;
                result = (void*)(virt + sizeof(LargeAllocHeader));
            }
        }
    }

    HeapUnlock();
    return result;
}

void KernelHeap::Free(void *ptr)
{
    if (!ptr) return;

    HeapLock();

    uintptr_t addr = (uintptr_t)ptr;

    if (addr < HEAP_BASE || addr >= HEAP_END)
    {
        HeapUnlock();
        PanicContext ctx = {};
        ctx.message = "KernelHeap::Free: pointer outside heap range";
        KernelPanic(ctx);
        return;
    }

    uintptr_t page_base = addr & ~(PAGE_SIZE - 1);
    LargeAllocHeader *hdr = (LargeAllocHeader*)page_base;

    if (hdr->magic == LARGE_ALLOC_MAGIC)
    {
        uint64_t virt_base  = hdr->virt_base;
        uint64_t phys_base  = hdr->phys_base;
        uint64_t requested_size = hdr->requested_size;
        uint32_t page_count = hdr->page_count;
        uint32_t order      = hdr->order;

        hdr->magic = 0;

        for (uint32_t i = 0; i < page_count; i++)
        {
            virtualMemoryManager.UnmapPage(
                virt_base + (uint64_t)i * PAGE_SIZE);
        }

        PMMFree(&g_pmm, phys_base, order);
        total_allocated -= requested_size;
    }
    else
    {
        SlabHeader *slab = (SlabHeader*)page_base;
        size_t idx = CacheIndexFor(slab->obj_size);
        if (idx >= SLAB_CLASS_COUNT)
        {
            HeapUnlock();
            PanicContext ctx = {};
            ctx.message = "KernelHeap::Free: corrupted slab header or double-free";
            KernelPanic(ctx);
            return;
        }

        total_allocated -= slab->obj_size;
        caches[idx].Free(ptr);
    }

    HeapUnlock();
}

void *KernelHeap::AllocZeroed(size_t size)
{
    void *ptr = Alloc(size);
    if (ptr) kmemset_zero(ptr, size);
    return ptr;
}

void *KernelHeap::Realloc(void *ptr, size_t new_size)
{
    if (!ptr) return Alloc(new_size);
    if (!new_size)
    {
        Free(ptr);
        return nullptr;
    }

    // Find old size from slab or large header
    size_t old_size = 0;
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t page_base = addr & ~(PAGE_SIZE - 1);

    LargeAllocHeader *hdr = (LargeAllocHeader*)page_base;

    if (hdr->magic == LARGE_ALLOC_MAGIC)
    {
        old_size = (size_t)hdr->page_count * PAGE_SIZE - sizeof(LargeAllocHeader);
    }
    else
    {
        // uintptr_t page_base = addr & ~(PAGE_SIZE - 1);
        SlabHeader *slab = (SlabHeader*)page_base;
        old_size = slab->obj_size;
    }

    // If it already fits in the same size class, return as-is
    if (new_size <= old_size && CacheIndexFor(new_size) == CacheIndexFor(old_size)) return ptr;

    void *new_ptr = Alloc(new_size);
    if (!new_ptr) return nullptr;

    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    kmemcpy_bytes(new_ptr, ptr, copy_size);
    Free(ptr);
    return new_ptr;
}

void SlabCache::Init(size_t size)
{
    obj_size = size;
    partial = nullptr;
    full = nullptr;
    total_allocs = 0;
    total_frees = 0;
}

// Allocate a new slab page from PMM, map it, carve into slots,
// and prepend to the partial list
bool SlabCache::GrowSlab()
{
    uint64_t phys = 0;
    uint64_t virt = kernelHeap.MapHeapPages(1, &phys, 0);
    if (!virt) return false;

    SlabHeader *slab = (SlabHeader*)virt;

    // How many objects fit after the header?
    size_t header_size = sizeof(SlabHeader);
    // Round header up to obj_size alignment so slots are natually aligned
    size_t aligned_header = (header_size + obj_size - 1) & ~(obj_size - 1);

    uint32_t slots = (uint32_t)((PAGE_SIZE - aligned_header) / obj_size);
    if (slots == 0)
    {
        // obj_size is so large it doesn't fit even once- shouldn't happen
        // given the size classes, but guard anyway
        virtualMemoryManager.UnmapPage(virt);
        PMMFree(&g_pmm, phys, 0);
        return false;
    }

    slab->next = nullptr;
    slab->prev = nullptr;
    slab->free_count = slots;
    slab->total = slots;
    slab->obj_size = (uint32_t)obj_size;
    slab->_pad = 0;

    // Build the freelist: each slot's first pointer-width bytes point to next
    uint8_t *slot_base = (uint8_t*)virt + aligned_header;
    FreeSlot *prev_slot = nullptr;
    FreeSlot *first = nullptr;

    for (uint32_t i = 0; i < slots; i++)
    {
        FreeSlot *slot = (FreeSlot*)(slot_base + i * obj_size);
        slot->next = nullptr;
        if (prev_slot) prev_slot->next = slot;
        else first = slot;
        prev_slot = slot;
    }

    slab->free_head = first;

    // Prepend to partial list
    slab->next = partial;
    slab->prev = nullptr;
    if (partial) partial->prev = slab;
    partial = slab;

    return true;
}

// Move a slab between lists
void SlabCache::MoveToFull(SlabHeader *slab)
{
    // Remove from partial
    if (slab->prev) slab->prev->next = slab->next;
    else partial = slab->next;
    if (slab->next) slab->next->prev = slab->prev;

    // Prepend to full
    slab->prev = nullptr;
    slab->next = full;
    if (full) full->prev = slab;
    full = slab;
}

void SlabCache::MoveToPartial(SlabHeader *slab)
{
    // Remove from full
    if (slab->prev) slab->prev->next = slab->next;
    else full = slab->next;
    if (slab->next) slab->next->prev = slab->prev;

    // prepend to partial
    slab->prev = nullptr;
    slab->next = partial;
    if (partial) partial->prev = slab;
    partial = slab;
}

void *SlabCache::Alloc()
{
    // Grow if no partial slabs available
    if (!partial)
    {
        if (!GrowSlab()) return nullptr;
    }

    SlabHeader *slab = partial;
    FreeSlot *slot = (FreeSlot*)slab->free_head;

    // Pop from freelist
    slab->free_head = slot->next;
    slab->free_count--;

    if (slab->free_count == 0)
    {
        MoveToFull(slab);
    }

    total_allocs++;
    return (void*)slot;
}

void SlabCache::Free(void *ptr)
{
    // Recover the slab header from the page base of the pointer
    uintptr_t page_base = (uintptr_t)ptr & ~(PAGE_SIZE - 1);
    SlabHeader *slab = (SlabHeader*)page_base;

    bool was_full = (slab->free_count == 0);

    // Push slot back onto the slab freelist
    FreeSlot *slot = (FreeSlot*)ptr;
    slot->next = (FreeSlot*)slab->free_head;
    slab->free_head = slot;
    slab->free_count++;

    if (was_full)
    {
        MoveToPartial(slab);

        total_frees++;
    }
}