#pragma once

#include <stdint.h>
#include "processes/scheduler.h"
#include "fat32.h"

struct limine_file;
#ifdef __cplusplus
extern "C" {
#endif
struct limine_file* GetFileLimine(const char* name);
#ifdef __cplusplus
}
#endif

// Virtual address space constants for user processes
static constexpr uint64_t USER_LOAD_BASE = 0x0000000000400000ULL;   // Flat binary loader is here
static constexpr uint64_t USER_STACK_TOP = 0x00007FFFFFFFE000ULL;
static constexpr uint64_t USER_STACK_SIZE = 0x0000000000008000ULL;  // 32KiB (8 pages)

/**
 * @brief Load a flat binary from the FAT32 volume at `path` into a fresh user
 * process.  The binary is mapped RX at USER_LOAD_BASE; a RW/NX stack is
 * mapped below USER_STACK_TOP.
 *
 * Returns a ready-to-schedule Process* on success, nullptr on any failure.
 * The caller is responsible for calling UThreadCreate() and enqueuing.
 *
 * On failure every physical page that was allocated is freed and the
 * AddressSpace is left in a safe (unmapped) state.
 */
Process* LoadBinary(FatVolume* vol, const char* path);

/**
 * @brief Load a flat binary into an already-initialised AddressSpace using
 * per-page (order-0) physical allocations, so the space can later be torn down
 * page-by-page with AddressSpace::DestroyUser().
 *
 * Maps the code RX at USER_LOAD_BASE and a RW/NX stack below USER_STACK_TOP.
 * Does NOT create a Process or Task — the caller owns `as`. Used by exec().
 *
 * Returns true on success; on failure any pages mapped so far are left for the
 * caller to reclaim via DestroyUser().
 */
bool LoadBinaryInto(AddressSpace* as, FatVolume* vol, const char* path);

/**
 * @brief Load a flat binary from an in-memory buffer (e.g. a Limine module)
 * into a fresh user process. Same layout as LoadBinary().
 */
Process* LoadBinaryFromMemory(const void* data, uint32_t size);

/**
 * @brief Like LoadBinaryInto(), but the image bytes come from memory instead of
 * FAT. Used by exec() when booting from ISO with no ATA disk.
 */
bool LoadBinaryIntoFromMemory(AddressSpace* as, const void* data, uint32_t size);