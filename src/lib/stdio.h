#pragma once

/**
 * @breif   Writes a formatted string to the screen terminal using variadic arguments
 * @details     Supports format specifiers: %%s (string), %%d (integer), %%x (hex), %%c (char)
 * @param[in]   fmt Null-terminated format control string
 * @param[in]   ... Variadic arguments matching format specifiers
 */
void kprintf(const char *fmt, ...);