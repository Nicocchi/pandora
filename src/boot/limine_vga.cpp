#include "limine_vga.h"
#include <lib/string.h>

#include <stdarg.h>

KRenderer kRenderer = {0};

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

    for (uint64_t y = 0; y < kRenderer.framebuffer.height; y++)
    {
        for (uint64_t x = 0; x < kRenderer.framebuffer.width; x++)
        {
            uint32_t *pixel_address = (uint32_t*)((uintptr_t)kRenderer.framebuffer.address
                                        + y * kRenderer.framebuffer.pitch
                                        + x * 4);
        }
    }

    if (reset)
    {
        kRenderer.x = 0;
        kRenderer.y = 0;
    }
}

// Internal helper function to convert numbers to ASCII strings without an allocator
static void itoa(uint64_t value, char *str, int base)
{
    char *rc = str;
    char *ptr = str;
    char *low;

    // Check for supported bases
    if (base < 2 || base > 16)
    {
        *str = '\0';
        return;
    }

    // Set up digits map
    const char* digits = "0123456789abcdef";
    
    // Extract digits in reverse order
    do {
        *ptr++ = digits[value % base];
        value /= base;
    } while (value);

    *ptr = '\0';

    // Terminate string and reverse the characters in-place
    low = rc;
    ptr--;
    while (low < ptr)
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
}

// void itoa(uint64_t value, char* buf, int base)
// {
//     char* p = buf;
//     char* p1, *p2;
//     uint64_t tmp;

//     do {
//         tmp = value % base;
//         *p++ = "0123456789abcdef"[tmp];
//         value /= base;
//     } while (value);

//     *p = '\0';

//     // reverse string
//     p1 = buf;
//     p2 = p - 1;

//     while (p1 < p2)
//     {
//         char c = *p1;
//         *p1++ = *p2;
//         *p2-- = c;
//     }
// }

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] == '%')
        {
            i++;

            switch (fmt[i])
            {
                case 's':
                {
                    const char *s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s)
                    {
                        DrawChar(*s++);
                    }
                } break;

                case 'd':
                {
                    int64_t d = va_arg(args, int);
                    char buf[32];
                    if (d < 0)
                    {
                        DrawChar('-');
                        d = -d;
                    }
                    itoa(d, buf, 10);
                    for (size_t j = 0; buf[j] != '\0'; j++) DrawChar(buf[j]);
                } break;

                case 'x':
                {
                    // 32-bit hex
                    uint32_t x = va_arg(args, uint32_t);
                    char buf[32];
                    itoa(x, buf, 16);
                    for (size_t j = 0; buf[j] != '\0'; j++) DrawChar(buf[j]);
                } break;
                case 'l':
                {
                    // look ahead for llx
                    if (fmt[i+1] == 'l' && fmt[i+2] == 'x')
                    {
                        i += 2;

                        uint64_t val = va_arg(args, uint64_t);

                        char buf[32];
                        itoa(val, buf, 16);

                        for (size_t j = 0; buf[j]; j++) DrawChar(buf[j]);
                    }
                } break;

                case 'p':
                {
                    uint64_t ptr = (uint64_t)va_arg(args, void*);

                    DrawChar('0');
                    DrawChar('x');

                    char buf[32];
                    itoa(ptr, buf, 16);

                    for (size_t j = 0; buf[j]; j++) DrawChar(buf[j]);
                } break;

                case 'c':
                {
                    char c = (char)va_arg(args, int);
                    DrawChar(c);
                } break;

                case '%':
                {
                    DrawChar('%');
                } break;

                default:
                {
                    DrawChar('%');
                    DrawChar(fmt[i]);
                } break;
            }
        }
        else {
            DrawChar(fmt[i]);
        }
    }

    va_end(args);
}