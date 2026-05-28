/**
 * @file        string.h
 * @brief       Freestanding standard utility functions for raw memory and string diagnostics.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 27, 2026
 * 
 * @details     Implements clean-room equivalents of standard C library memory transformations
 *              (`memcpy`, `memset`, `memmove`, `memcmp`) and primitive null-terminated ASCII string
 *              processing rules. Designed specifically to execute without a standard C library runtime
 *              environment.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief       Copies a chunk of memory from a source buffer to a non-overlapping destination buffer.
 * @warning     Behavior is undefined if the source and destination buffers overlap. Use `memmove` if overlap is possible.
 * @param[out]  dest Pointer to the destination memory area where bytes are written.
 * @param[in]   src  Pointer to the source memory area from which bytes are read.
 * @param[in]   n    The exact number of bytes to copy.
 * @return      void* A pointer to the destination memory block (`dest`).
 */
void *memcpy(void *__restrict dest, const void *__restrict src, size_t n);

/**
 * @brief       Fills the first `n` bytes of a memory block with a constant byte value.
 * @param[out]  s    Pointer to the memory area to be filled.
 * @param[in]   c    The byte value to copy into the target memory block (cast internally to `uint8_t`).
 * @param[in]   n    The number of bytes to overwrite.
 * @return      void* A pointer to the filled memory destination area (`s`).
 */
void *memset(void *s, int c, size_t n);

/**
 * @brief       Copies a chunk of memory from a source buffer to a destination buffer, safely handling overlaps.
 * @details     Compares the address locations of both buffers to determine if copying must be executed 
 *              forwards or backwards, protecting against memory corruption or self-overwriting.
 * @param[out]  dest Pointer to the destination memory area.
 * @param[in]   src  Pointer to the source memory area.
 * @param[in]   n    The exact number of bytes to transfer.
 * @return      void* A pointer to the destination memory block (`dest`).
 */
void *memmove(void *dest, const void *src, size_t n);

/**
 * @brief       Compares the first `n` bytes of two independent memory regions.
 * @param[in]   s1 Pointer to the first memory area to evaluate.
 * @param[in]   s2 Pointer to the second memory area to evaluate.
 * @param[in]   n  The byte length bound to constrain the comparison loop.
 * @return      int Negative value if `s1` is less than `s2`, positive if `s1` is greater than `s2`, or 0 if identical.
 */
int memcmp(const void *s1, const void *s2, size_t n);

/**
 * @brief       Calculates the length of a null-terminated string array.
 * @note        Does not include the termination byte (`\0`) in the counted total length.
 * @param[in]   s Array pointer holding the target string.
 * @return      int The number of valid characters before reaching the termination byte tracker.
 */
int strlen(const char s[]);

/**
 * @brief       Verifies whether a string ends with a specific trailing substring.
 * @details     Extremely useful during boot operations to parse and validate filename extensions (e.g., checking for ".psf").
 * @param[in]   str The base null-terminated target string to analyze.
 * @param[in]   end The expected suffix string to check against the end of `str`.
 * @return      true if `str` safely terminates with the exact characters matching `end`, otherwise false.
 */
bool CheckStringEndsWith(const char *str, const char *end);

#ifdef __cplusplus
}
#endif

