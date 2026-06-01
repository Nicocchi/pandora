#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include "stdio.h"
#include "boot/limine_vga.h"

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

void kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] != '%')
        {
            DrawChar(fmt[i]);
            continue;
        }

        i++; // consume '%'

        // Flags
        bool flag_zero = false;     // zero-pad instead of space-pad
        bool flag_left = false;     // left-align (overrides zero-pad)
        bool flag_plus = false;     // always show sign for signed types
        bool flag_space = false;    // space before positive signed value
        bool flag_hash = false;     // perfix: 0x for hex, 0 for octal

        bool parsing_flags = true;
        while (parsing_flags)
        {
            switch (fmt[i])
            {
                case '0': flag_zero = true; i++; break;
                case '-': flag_left = true; i++; break;
                case '+': flag_plus = true; i++; break;
                case ' ': flag_space = true; i++; break;
                case '#': flag_hash = true; i++; break;
                default: parsing_flags = false; break;
            }
        }

        // Width
        int width = 0;
        if (fmt[i] == '*')
        {
            width = va_arg(args, int);
            if (width < 0) { flag_left = true; width = -width; }
            i++;
        }
        else
        {
            while (fmt[i] >= '0' && fmt[i] <= '9')
            {
                width = width * 10 + (fmt[i++] - '0');
            }
        }

        // Precision
        int precision = -1; // -1 = not specified
        if (fmt[i] == '.')
        {
            i++;
            precision = 0;
            if (fmt[i] == '*')
            {
                precision = va_arg(args, int);
                if (precision < 0) precision = 0;
                i++;
            }
            else
            {
                while (fmt[i] >= '0' && fmt[i] <= '9')
                {
                    precision = precision * 10 + (fmt[i++] - '0');
                }
            }
        }

        // Length modifier
        // hh, h, l, ll, z, t
        enum LengthMod { LEN_DEFAULT, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_T };
        LengthMod len = LEN_DEFAULT;

        if (fmt[i] == 'h')
        {
            i++;
            if (fmt[i] == 'h') { len = LEN_HH; i++; }
            else len = LEN_H;
        } else if (fmt[i] == 'l')
        {
            i++;
            if (fmt[i] == 'l') { len = LEN_LL; i++; }
            else len = LEN_L;
        } else if (fmt[i] == 'z') { len = LEN_Z; i++; }
        else if (fmt[i] == 't') { len = LEN_T; i++; }

        // Specifier
        char spec = fmt[i];

        // Helper: emit a string with padding
        auto emit_padded = [&](const char *s, int slen)
        {
            int pad = (width > slen) ? (width - slen) : 0;
            char pc = (flag_zero && !flag_left) ? '0' : ' ';

            if (!flag_left)
            {
                for (int p = 0; p < pad; p++) DrawChar(pc);
            }

            for (int p = 0; p < slen; p++) DrawChar(s[p]);

            if (flag_left)
            {
                for (int p = 0; p < pad; p++) DrawChar(' ');
            }
        };

        switch (spec)
        {
            // Signed decimal: %d / %i / %ld / %lld / %hd / %hhd
            case 'd':
            case 'i':
            {
                int64_t val;
                switch(len)
                {
                    case LEN_LL: val = va_arg(args, long long); break;
                    case LEN_L: val = va_arg(args, long); break;
                    case LEN_HH: val = (signed char)va_arg(args, int); break;
                    case LEN_H: val = (short)va_arg(args, int); break;
                    default: val = va_arg(args, int); break;
                }

                char buf[32];
                bool negative = (val < 0);
                if (negative) val = -val;
                itoa((uint64_t)val, buf, 10);

                int numlen = 0;
                while (buf[numlen]) numlen++;

                // Sign character
                char sign = 0;
                if (negative) sign = '-';
                else if (flag_plus) sign = '+';
                else if (flag_space) sign = ' ';

                int total = numlen + (sign ? 1 : 0);
                int pad = (width > total) ? (width - total) : 0;
                char pc = (flag_zero && !flag_left) ? '0' : ' ';

                if (!flag_left) for (int p = 0; p < pad; p++) DrawChar(pc);
                if (sign) DrawChar(sign);
                for (int p = 0; buf[p]; p++) DrawChar(buf[p]);
                if (flag_left) for (int p = 0; p < pad; p++) DrawChar(' ');
            } break;

            // Unsigned decimal: %u / %lu / %llu
            case 'u':
            {
                uint64_t val;
                switch (len)
                {
                    case LEN_LL: val = va_arg(args, unsigned long long); break;
                    case LEN_L: val = va_arg(args, unsigned long); break;
                    case LEN_HH: val = (unsigned char)va_arg(args, int); break;
                    case LEN_H: val = (unsigned short)va_arg(args, int); break;
                    case LEN_Z: val = va_arg(args, size_t); break;
                    default: val = va_arg(args, unsigned int); break;
                }
                char buf[32];
                itoa(val, buf, 10);
                int len2 = 0; while (buf[len2]) len2++;
                emit_padded(buf, len2);
            } break;

            // Hex: %x / %X / %lx / %llx
            case 'x':
            case 'X':
            {
                uint64_t val;
                switch (len)
                {
                    case LEN_LL: val = va_arg(args, unsigned long long); break;
                    case LEN_L: val = va_arg(args, unsigned long); break;
                    case LEN_HH: val = (unsigned char)va_arg(args, int); break;
                    case LEN_H: val = (unsigned short)va_arg(args, int); break;
                    default: val = va_arg(args, unsigned int); break;
                }
                char buf[32];
                itoa(val, buf, 16);

                // Uppercase if %X
                if (spec == 'X')
                {
                    for (int p = 0; buf[p]; p++)
                    {
                        if (buf[p] >= 'a' && buf[p] <= 'f')
                        {
                            buf[p] -= 32;
                        }
                    }
                }

                int numlen = 0; while (buf[numlen]) numlen++;
                int prefix_len = (flag_hash && val != 0) ? 2 : 0;
                int total = numlen + prefix_len;
                int pad = (width > total) ? (width - total) : 0;
                char pc = (flag_zero && !flag_left) ? '0' : ' ';

                if (!flag_left) for (int p = 0; p < pad; p++) DrawChar(pc);
                if (flag_hash && val != 0) { DrawChar('0'); DrawChar(spec == 'X' ? 'X' : 'x'); }
                for (int p = 0; buf[p]; p++) DrawChar(buf[p]);
                if (flag_left)  for (int p = 0; p < pad; p++) DrawChar(' ');
            } break;

            // Octal: %o
            case 'o':
            {
                uint64_t val;
                switch (len)
                {
                    case LEN_LL: val = va_arg(args, unsigned long long); break;
                    case LEN_L: val = va_arg(args, unsigned long); break;
                    default: val = va_arg(args, unsigned int); break;
                }
                char buf[32];
                itoa(val, buf, 8);
                int len2 = 0; while (buf[len2]) len2++;
                if (flag_hash && buf[0] != '0')
                {
                    // prepend '0'
                    for (int p = len2; p >= 0; p--) buf[p+1] = buf[p];
                    buf[0] = '0'; len2++;
                }
                emit_padded(buf, len2);
            } break;

            // Binary: %b
            case 'b':
            {
                uint64_t val;
                switch (len)
                {
                    case LEN_LL: val = va_arg(args, unsigned long long); break;
                    case LEN_L: val = va_arg(args, unsigned long); break;
                    default: val = va_arg(args, unsigned int); break;
                }
                char buf[66];
                int pos = 0;
                if (flag_hash) { buf[pos++] = '0'; buf[pos++] = 'b'; }
                bool leading = true;
                for (int bit = 63; bit >= 0; bit--)
                {
                    if ((val >> bit) & 1) { buf[pos++] = '1'; leading = false; }
                    else if (!leading) buf[pos++] = '0';
                }
                if (leading) buf[pos++] = '0'; // val == 0
                buf[pos] = '\0';
                emit_padded(buf, pos);
            } break;

            // Pointer: %p
            case 'p':
            {
                uint64_t val = (uint64_t)va_arg(args, void*);
                DrawChar('0'); DrawChar('x');
                char buf[32];
                itoa(val, buf, 16);
                for (int p = 0; buf[p]; p++) DrawChar(buf[p]);
            } break;

            // String: %s
            case 's':
            {
                const char *s = va_arg(args, const char*);
                if (!s) s = "(null)";
                int slen = 0;
                while (s[slen]) slen++;
                if (precision >= 0 && precision < slen) slen = precision;
                emit_padded(s, slen);
            } break;

            // Char: %c
            case 'c':
            {
                char c = (char)va_arg(args, int);
                if (width > 1 && !flag_left)
                {
                    for (int p = 0; p < width - 1; p++) DrawChar(' ');
                }
                DrawChar(c);
                if (width > 1 && flag_left)
                {
                    for (int p = 0; p < width - 1; p++) DrawChar(' ');
                }
            } break;

            // %%
            case '%':
            {
                DrawChar('%');
            } break;

            // Unknwon: pass through
            default:
            {
                DrawChar('%');
                DrawChar(spec);
                break;
            }
        }
    }

    va_end(args);
}