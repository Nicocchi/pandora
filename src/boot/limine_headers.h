/**
 * @file        limine_headers.h
 * @brief       Bootloader handshake configurations, request definitions, and asset retrieval
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 27, 2026
 * 
 * @details     Defines the mandatory protocol anchors, base revision limits, and system requests
 *              communicated directly to the Limine Bootloader. Places structural protocol structures
 *              into specialized ELF sections parsed during kernel initialization.
 * 
 *              References:
 *              - Limine Bare Bones Template: https://wiki.osdev.org/Limine_Bare_Bones
 *              - Limine Boot Protocol Specification: https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <boot/limine.h>
#include <lib/string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup    Limine_Anchors Limine Protocol Anchors
 * @brief       Limine protocol requests placed into dedicated ELF sections for compiler section layout mapping
 * @{
 */

 /** @brief Informs Limine of the specific protocol revision level required by this kernel */
__attribute__((used, section(".limine_requests")))
volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

/** @brief Requests a graphical linear framebuffer layout configuration from the bootloader */
__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// /** @brief Requests the physical memory map array topology from the BIOS/UEFI firmware */
// __attribute__((used, section(".limine_requests")))
// volatile struct limine_memmap_request memmap_request = {
//     .id = LIMINE_MEMMAP_REQUEST_ID,
//     .revision = 0
// };

/** @brief Requests external boot modules (e.g., ramdisks, fonts, assets) defined in limine.conf */
__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0
};

/** @brief Safeguard memory marker establishing the structural boundary start of the request section */
__attribute__((used, section(".limine_requests_start")))
volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

/** @brief Safeguard memory marker establishing the structural boundary end of the request section */
__attribute__((used, section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

/** @} */

/**
 * @brief   Scans loaded bootloader modules to locate an asset file matching a specific name suffix
 *
 * Iterates over the raw memory pointers populated by the Limine bootloader's module response block.
 * Uses path suffix matching to locate assets independently of full physical device directory prefixes.
 *
 * @param[in]   name The target filename or trailing file path segment to match (e.g., "zap-light16.psf")
 *
 * @return      struct limine_file* Pointer to the matching Limine file structure holding memory
 *              addresses and sizing limits, or `nullptr` if the asset could not be located.
 */
static inline struct limine_file *GetFileLimine(const char *name)
{
    struct limine_module_response *module_response = module_request.response;
    if (module_response == NULL) return NULL;

    for (size_t i = 0; i < module_response->module_count; i++)
    {
        struct limine_file *f = module_response->modules[i];
        if (f && f->path)
        {
            if (CheckStringEndsWith(f->path, name)) return f;
        }
    }
    return nullptr;
}

#ifdef __cplusplus
}
#endif
