/**
 * @file        pmm.h
 * @brief       Physical Memory Manager (Buddy Allocator) declarations
 *
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 *
 * @author      Nicocchi
 * @date        May 31, 2026
 *
 * @details     This header defines a binary buddy memory allocator for managing
 *              physical page frames. It tracks free memory blocks using an intrusive circular
 *              doubly linked list per allocation order, supplemented by a bitmap tracking 
 *              allocation states to prevent fragmentation and facilitate coalescing.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "boot/limine.h"

/** @brief Standard physical page frame size (4 Kibibytes). */
static constexpr uint64_t PAGE_SIZE = 0x1000;

/** @brief Bit-shift count to convert bytes to pages and vice versa (\f$2^{12} = 4096\f$). */
static constexpr uint64_t PAGE_SHIFT = 12;

/** 
 * @brief Maximum allocation order supported by the allocator.
 * 
 * @details Represents block orders from 0 to 10. The maximum contiguous block 
 *          size is \f$2^{10} \times 4\text{ KiB} = 4\text{ MiB}\f$.
 */
static constexpr uint8_t MAX_ORDER = 11;

/**
 * @brief   Free-list node structure.
 * @details Embedded intrusively directly inside unallocated, free physical memory blocks
 *          to eliminate tracking memory overhead.
 */
struct BuddyBlock
{
    BuddyBlock *next;   /**< Pointer to the next free block in the sequence. */
    BuddyBlock *prev;   /**< Pointer to the previous free block in the sequence. */
};

/**
 * @brief   Per-order free list manager.
 * @details Implements an intrusive, circular doubly-linked list featuring a sentinel head node 
 *          to ensure \f$O(1)\f$ insertion, removal, and retrieval operations.
 */
struct FreeList
{
    BuddyBlock head;    /**< Circular list sentinel node. Does not represent an actual free block. */
    uint64_t count;     /**< Total number of active free blocks currently available at this order. */

    /**
     * @brief Initializes the free list.
     * @note  Sets up the circular sentinel relationship by pointing the head to itself.
     */
    void init();

    /**
     * @brief  Pushes a memory block to the front of the list.
     * @param  blk Pointer to the buddy block node to insert.
     */
    void push(BuddyBlock *blk);

    /**
     * @brief  Pops and retrieves the first available block from the list.
     * @return Pointer to the extracted BuddyBlock, or @c nullptr if the list is empty.
     */
    BuddyBlock *pop();

    /**
     * @brief  Removes a specific block from anywhere within the list.
     * @param  blk Pointer to the block node to isolate and extract.
     */
    void remove(BuddyBlock *blk);

    /**
     * @brief  Validates whether a specific block resides within this list layer.
     * @param  blk Pointer to the block node to verify.
     * @return @c true if the block is tracked by this list; otherwise, @c false.
     */
    bool contains(BuddyBlock *blk) const;

    /**
     * @brief  Checks whether the free list contains zero blocks.
     * @return @c true if empty; @c false if tracked nodes exist.
     */
    bool empty() const { return head.next == &head; }
};

/**
 * @brief   Core Buddy Allocator control structure.
 * @details Manages the physical address space mapped out via memory tracking structures,
 *          supporting fast coalescing of neighboring buddies upon block deallocation.
 */
struct BuddyAllocator
{
    FreeList lists[MAX_ORDER];  /**< Array of free lists tracking blocks sorted by size order. */
    uint8_t *bitmap;            /**< Allocation metadata bitmap mapping bit status per page block. */
    uint64_t bitmap_size;       /**< Total size of the tracking bitmap in bytes. */
    uint64_t base;              /**< Base physical address of the managed memory pool region. */
    uint64_t total_pages;       /**< Cumulative page frames assigned to the allocator instance. */
    uint64_t free_pages;        /**< Total counts of volatile unallocated page frames remaining. */

    /**
     * @brief  Initializes the tracking structures using bootloader configurations.
     * @param  mmap Pointer to the Limine bootloader physical memory map response structure.
     */
    void Init (limine_memmap_response *mmap);

    /**
     * @brief  Allocates a contiguous block of physical pages matching the requested order.
     * @param  order Power-of-two size exponent calculation index (\f$2^{\text{order}}\f$ pages).
     * @return Physical base address of the allocated block, or @c 0 on allocation failure.
     */
    uint64_t Alloc(uint8_t order);

    /**
     * @brief  Frees an allocated memory block and attempts buddy coalescing.
     * @param  addr  The physical base address of the block to return to the pool.
     * @param  order The power-of-two size exponent calculation index used during initial allocation.
     */
    void Free(uint64_t addr, uint8_t order);
};

/**
 * @brief  Convenience C-style wrapper function for allocating physical memory.
 * @param  ba    Pointer to the active allocator instance context.
 * @param  order Requested allocation block size order (defaults to @c 0 for single page).
 * @return Physical base address of allocated memory block, or @c 0 on failure.
 */
inline uint64_t PMMAlloc(BuddyAllocator* ba, uint8_t order = 0)
{
    return ba->Alloc(order);
}

/**
 * @brief  Convenience C-style wrapper function for freeing physical memory blocks.
 * @param  ba    Pointer to the active allocator instance context.
 * @param  addr  Physical base address of the block returning to the system pool.
 * @param  order Block size allocation order index matching original allocation parameters.
 */
inline void PMMFree(BuddyAllocator* ba, uint64_t addr, uint8_t order = 0)
{
    ba->Free(addr, order);
}