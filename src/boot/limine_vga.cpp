#include "limine_vga.h"
#include <lib/string.h>

KRenderer kRenderer = {0};

static int console_lock_depth = 0;
static uint64_t console_lock_flags = 0;

void ConsoleLock()
{
    if (console_lock_depth++ == 0)
    {
        asm volatile("pushfq; pop %0; cli" : "=r"(console_lock_flags) : : "memory");
    }
}

void ConsoleUnlock()
{
    if (console_lock_depth > 0 && --console_lock_depth == 0)
    {
        asm volatile("push %0; popfq" :: "r"(console_lock_flags) : "memory", "cc");
    }
}

static void ScrollScreen()
{
    uint64_t font_height = kRenderer.font.psf1_Header->characterSize;
    uint64_t bytes_per_pixel = 4; // 32-bit ARGB

    uint64_t row_bytes = kRenderer.framebuffer.pitch * font_height;

    uint64_t shift_height = kRenderer.framebuffer.height - font_height;
    uint64_t shift_bytes = kRenderer.framebuffer.pitch * shift_height;

    memmove(kRenderer.framebuffer.address, (void*)((uintptr_t)kRenderer.framebuffer.address + row_bytes),
            shift_bytes);
    
    uint32_t *bottom_row_ptr = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address + shift_bytes);
    uint64_t remaining_pixels = (kRenderer.framebuffer.pitch / bytes_per_pixel) * font_height;

    for (uint64_t i = 0; i < remaining_pixels; i++)
    {
        bottom_row_ptr[i] = BLACK;
    }

    kRenderer.y -= font_height;
}

void DrawChar(char c)
{
    uint64_t font_height = kRenderer.font.psf1_Header->characterSize;
    uint64_t font_width = 8;

    if (c == '\b')
    {
        if (kRenderer.x >= font_width)
        {
            kRenderer.x -= font_width;  // Step the horizontal cursor backward
        } else if (kRenderer.y >= font_height)
        {
            // Wrap backward to the previous screen line if at the left margin
            kRenderer.x = (kRenderer.framebuffer.width / font_width) * font_width - font_width;
            kRenderer.y -= font_height;
        }

        // Wipe the previous character with a blank block of the background color
        uintptr_t base_address = (uintptr_t)(kRenderer.framebuffer.address);
        for (uint64_t py = 0; py < font_height; py++)
        {
            for (uint64_t px = 0; px < font_width; px++)
            {
                uint32_t *pixel_address = (uint32_t*)(base_address
                                            + (kRenderer.y + py) * kRenderer.framebuffer.pitch
                                            + (kRenderer.x + px) * 4);
                *pixel_address = BLACK;
            }
        }

        return;
    }

    if (c == '\n')
    {
        kRenderer.x = 0;
        kRenderer.y += font_height;

        if (kRenderer.y + font_height > kRenderer.framebuffer.height)
        {
            ScrollScreen();
        }
        return;
    }

    if (c == '\r')
    {
        kRenderer.x = 0;
        return;
    }

    if (c == '\t')
    {
        kRenderer.x += font_width * 4;
        return;
    }

    if (kRenderer.x + font_width > kRenderer.framebuffer.width)
    {
        kRenderer.x = 0;
        kRenderer.y += font_height;
    }

    if (kRenderer.y + font_height > kRenderer.framebuffer.height)
    {
        ScrollScreen();
    }

    uint8_t *glyph = (uint8_t*)kRenderer.font.glyphBuffer + ((uint8_t)c * font_height);
    for (uint64_t py = 0; py < font_height; py++)
    {
        for (uint64_t px = 0; px < font_width; px++)
        {
            if (glyph[py] & (0x80 >> px))
            {
                uint32_t *pixel_address = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address
                                            + (kRenderer.y + py) * kRenderer.framebuffer.pitch
                                            + (kRenderer.x + px) * 4);
                *pixel_address = kRenderer.color;
            }
        }
    }

    kRenderer.x += font_width;
}

void ClearScreen(uint32_t color, bool reset)
{
    uint64_t ppl = kRenderer.framebuffer.pitch / sizeof(uint32_t);

    for (uint64_t y = 0; y < kRenderer.framebuffer.height; y++)
    {
        uint32_t *row = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address
                                    + y * kRenderer.framebuffer.pitch);
        for (uint64_t x = 0; x < ppl; x++)
        {
            row[x] = color;
        }
    }

    if (reset)
    {
        kRenderer.x = 0;
        kRenderer.y = 0;
    }
}