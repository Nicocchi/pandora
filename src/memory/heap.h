#pragma once

#include <stdint.h>
#include <stddef.h>
#include "vmm.h"

extern VirtualMemoryManager virtualMemoryManager;

static constexpr uint64_t HEAP_BASE = 0xFFFFC00000000000ULL;
static constexpr uint64_t HEAP_SIZE = 0x0001000000000000ULL; // 16 TiB ceiling
static constexpr uint64_t HEAP_END  = 0xFFFFD00000000000ULL;

// Number of small size classes
static constexpr size_t SLAB_CLASS_COUNT = 10;

// Size classes in bytes
static constexpr size_t SLAB_SIZES[SLAB_CLASS_COUNT] =
{
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096
};

static constexpr size_t KMALLOC_LARGE_THRESHOLD = 4096;

struct SlabHeader
{
    SlabHeader *next;
    SlabHeader *prev;
    void *free_head;
    uint32_t free_count;
    uint32_t total;
    uint32_t obj_size;
    uint32_t _pad;
};

struct FreeSlot
{
    FreeSlot *next;
};

struct SlabCache
{
    size_t obj_size;
    SlabHeader *partial;
    SlabHeader *full;
    uint64_t total_allocs;
    uint64_t total_frees;

    void Init(size_t size);
    void *Alloc();
    void Free(void *ptr);
    bool GrowSlab();
    // Move a slab between lists
    void MoveToFull(SlabHeader *slab);
    void MoveToPartial(SlabHeader *slab);
};

struct LargeAllocHeader
{
    uint64_t magic;
    uint64_t virt_base;
    uint64_t phys_base;
    uint64_t requested_size;
    uint32_t page_count;
    uint32_t order;
};

static constexpr uint64_t LARGE_ALLOC_MAGIC = 0x4C41524741434BULL; // "LARGACK"

struct KernelHeap
{
    SlabCache caches[SLAB_CLASS_COUNT];
    uint64_t heap_virt_next;
    uint64_t total_allocated;
    uint64_t total_mapped;

    void Init();
    void *Alloc(size_t size);
    void Free(void *ptr);
    void *AllocZeroed(size_t size);
    void *Realloc(void *ptr, size_t new_size);
    uint64_t MapHeapPages(uint32_t pages, uint64_t *phys_out, uint8_t pmm_order);
};

extern KernelHeap kernelHeap;

inline void *kmalloc(size_t size)
{
    return kernelHeap.Alloc(size);
}

inline void *kzalloc(size_t size)
{
    return kernelHeap.AllocZeroed(size);
}

inline void *krealloc(void *ptr, size_t size)
{
    return kernelHeap.Realloc(ptr, size);
}

inline void kfree(void *ptr)
{
    if (ptr)
    {
        kernelHeap.Free(ptr);
    }
}

template<typename T>
inline T *knew() { return static_cast<T*>(kzalloc(sizeof(T))); }

template<typename T>
inline T *knew_array(size_t n) { return static_cast<T*>(kzalloc(sizeof(T) * n)); }