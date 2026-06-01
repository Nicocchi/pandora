/**
 * @file        limine_vga.h
 * @brief       Driver interface for the Limine graphics framebuffer and PSF1 text rendering
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 27, 2026
 * 
 * @details     Provides core definitions, color palettes, and structures required
 *              to manage the Limine bootloader's graphical framebuffer response.
 *              Includes explicit type mapping for PC Screen Font (PSF1) parsing
 *              to enable kernel-space text rendering and terminal emulation.
 * 
 *              References:
 *              - Limine Bare Bones Template: https://wiki.osdev.org/Limine_Bare_Bones
 *              - Limine Boot Protocol Specification: https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md
 *              - PC Screen Font Formats: https://wiki.osdev.org/PC_Screen_Font
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @defgroup    VGA_Colors 32-bit ARGB Color Palette
 * @brief       Standard color definitions utilizing 32-bit Alpha-Red-Green-Blue (0xAARRGGBB) format.
 * @{
 */
#define WHITE   0xFFFFFFFF  /**< Full intensity crisp white */
#define BLACK   0xFF000000  /**< Opaque solid black */
#define SILVER  0xFFC0C0C0  /**< Light metallic silver gray */
#define GRAY    0xFF808080  /**< Medium neutral gray */
#define RED     0xFF800000  /**< Standard dark maroon red */
#define YELLOW  0xFFFFD700  /**< Golden rod yellow */
#define BLUE    0xFF000080  /**< Classic deep navy blue */
#define CYAN    0xFF008080  /**< Dark teal cyan */
#define GREEN   0xFF008000  /**< Forest green */
#define PINK    0xFFFF1493  /**< Deep vibrant hot pink */
#define PURPLE  0xFF800080  /**< Royal purple */
#define ORANGE  0xFFFF4500  /**< High-visibility orange-red */
#define BROWN   0xFFA52A2A  /**< Earth-tone brown */
#define DBLUE   0xFF000030  /**< Very dark midnight blue background */
#define DGRAY   0xFF404040  /**< Dark charcoal gray */
#define BGRAY   0xFFC0C0C0  /**< Bright secondary gray */
#define BRED    0xFFFF0000  /**< Full intensity bright neon red */
#define BBLUE   0xFF0000FF  /**< Full intensity bright neon blue */
#define BGREEN  0xFF00FF00  /**< Full intensity bright neon green */
#define TBLACK  0x00000000  /**< Fully transparent black (Alpha = 0) */
/** @} */

/**
 * @defgroup    PSF1_Macros PC Screen Font v1 Constants
 * @brief       Magic signature bytes required to validate a PSF1 file header.
 * @{
 */
#define PSF1_MAGIC0 0x36    /**< First mandatory byte of the PSF1 identifier layout */
#define PSF1_MAGIC1 0x04    /**< Second mandatory byte of the PSF1 identifier layout */
/** @} */

/**
 * @defgroup    Console_Dimensions Cell buffer grid size limits
 * @brief       Maximum terminal grid dimensions. Sized for 1920x1080 with a 16px-tall
 *              PSF1 font (8x16 glyphs = 240 columns x 67 rows). Increase if using a
 *              higher resolution framebuffer or smaller font.
 * @{
 */
#define CONSOLE_MAX_COLS  256   /**< Maximum supported terminal column count */
#define CONSOLE_MAX_ROWS  128   /**< Maximum supported terminal row count */
/** @} */

/**
 * @struct PSF1_Header
 * @brief Binary header layout located at the start of a `.psf` font file.
 * 
 * For detailed specifications, see: https://wiki.osdev.org/PC_Screen_Font
 * 
 */
typedef struct PSF1_Header
{
    uint8_t magic[2];       /**< Magic signature bytes (`PSF1_MAGIC0`, `PSF1_MAGIC1`) */
    uint8_t mode;           /**< Font configuration mode flags (e.g., 256 vs 512 glyph sets) */
    uint8_t characterSize;  /**< Height of each individual glyph bitmap measured in pixels */
} PSF1_Header;

/**
 * @struct PSF1_Font
 * @brief Consolidated handle encapsulating file header layout and active glyph definitions.
 * 
 */
typedef struct PSF1_Font
{
    PSF1_Header *psf1_Header;   /**< Pointer mapping to the validated raw file header */
    void *glyphBuffer;          /**< Pointer to font data array (`limine_file->address + sizeof(PSF1_Header)`) */
} PSF1_Font;

/**
 * @struct KFramebuffer
 * @brief Encapsulates structural memory metrics for direct pixel manipulations
 * 
 */
typedef struct KFramebuffer
{
    void *address;              /**< Virtual base memory address pointing to the hardware pixel buffer */
    uint64_t width;             /**< Horizontal resolution boundary measured in total screen pixels */
    uint64_t height;            /**< Vertical resolution boundary measured in total screen pixels */
    uint64_t pitch;             /**< Total byte count allocating a single horizontal pixel row (Width * Bytes-Per-Pixel + Padding) */
    uint64_t pixelsPerScanLine; /**< Effective horizontal scanline offset length, defined natively as `pitch / 4` */
} KFramebuffer;

typedef struct ConsoleCell
{
    char c;
    uint32_t color;
} ConsoleCell;

/**
 * @struct KRenderer
 * @brief Core software rendering state machine controlling standard console tracking and styling
 * 
 */
typedef struct KRenderer
{
    KFramebuffer framebuffer;
    PSF1_Font font;
    uint32_t color;
    uint32_t bg_color;

    int cols;
    int rows;
    int cursor_col;
    int cursor_row;
    bool dirty;

    ConsoleCell cells[CONSOLE_MAX_ROWS][CONSOLE_MAX_COLS];

    // KFramebuffer framebuffer;   /**< Target graphics framebuffer layout configurations */
    // uint64_t x;                 /**< Active tracking state of terminal cursor's horizontal pixel column coordinate */
    // uint64_t y;                 /**< Active tracking state of the terminal cursor's vertical pixel row coordinate */
    // PSF1_Font font;             /**< Active font configuration mapping applied to standard character processing operations */
    // uint32_t color;             /**< Active target foreground color mapping (ARGB format) */

} KRenderer;

// KRenderer kRenderer = (KRenderer){0};
extern KRenderer kRenderer;

/**
 * @brief Draws an individual alphanumeric character directly into the graphics framebuffer memory space.
 * 
 * Blits individual bits from the active font glyph table directly to target hardware addresses.
 * Handles internal glyph alignment based on current tracking state parameters.
 * 
 * @param[in] c Target ASCII character intended for terminal pixel transformations
 */
void ConsoleLock();
void ConsoleUnlock();

void DrawChar(char c);

void FlushConsole();

/**
 * @brief Fills the entirety of the graphic display memory address range with a single uniform color state
 * 
 * @param[in] color Target standard ARGB coloring layout applied across the screen space
 * @param[in] reset Boolean flag stating if structural cursor layout variables (`x`, `y`) must be forced back to `0`
 */
void ClearScreen(uint32_t color, bool reset);
