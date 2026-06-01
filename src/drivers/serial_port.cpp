#include <stdint.h>
#include <stddef.h>
#include "serial_port.h"
#include <stdarg.h>

// Inline Assembly Wrappers for x86 port I/O
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}


int SerialInit(uint16_t port)
{
    outb(port + 1, 0x00);   // Disable all interrupts
    outb(port + 3, 0x80);   // Enable DLAB (set baud rate divisor)
    outb(port + 0, 0x03);   // Set divisor to 3 (lo byte) -> 38400 baud
    outb(port + 1, 0x00);   //                  (hi byte)
    outb(port + 3, 0x03);   // Disable DLAB, set 8 bits, no parity, 1 stop bit (8N1)
    outb(port + 2, 0xC7);   // Enable FIFO, clear them, with 14-byte threshold

#ifdef SERIAL_LOOPBACK_TEST
    // Perform Hardware Loopback Test to verify the UART is functional
    outb(port + 4, 0x1E);   // Set loopback mode, test latch IRQs
    outb(port + 0, 0xAE);   // Write a test byte (0xAE)
    if (inb(port + 0) != 0xAE) return 1; // Hardware error: loopback mismatch
    outb(port + 4, 0x0F);
#else
    // Cleanup up loopback mode and set normal operation mode
    outb(port + 4, 0x0F);   // IRQs enabled, turn on DTR, RTS, and OUT1/OUT2 (Normal Operation Mode)
#endif
    return 0;
}

static inline int IsTransmitEmpty(uint16_t port)
{
    return inb(port + 5) & 0x20; // Check Line Status Register Empty bit
}

void SerialWriteChar(uint16_t port, char c)
{
    while (IsTransmitEmpty(port) == 0); // Wait until the transmit buffer is clear
    outb(port, c);
}

static void SerialITOA(uint64_t value, char* buf, int base)
{
    char* p = buf;
    char* p1, *p2;
    uint64_t tmp;

    do {
        tmp = value % base;
        *p++ = "0123456789abcdef"[tmp];
        value /= base;
    } while (value);

    *p = '\0';

    // reverse string
    p1 = buf;
    p2 = p - 1;

    while (p1 < p2)
    {
        char c = *p1;
        *p1++ = *p2;
        *p2-- = c;
    }
}

void SerialWriteString(uint16_t port, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    for (size_t i = 0; fmt[i] != '\0'; i++)
    {
        if (fmt[i] != '%')
        {
            SerialWriteChar(port, fmt[i]);
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
                for (int p = 0; p < pad; p++) SerialWriteChar(port, pc);
            }

            for (int p = 0; p < slen; p++) SerialWriteChar(port, s[p]);

            if (flag_left)
            {
                for (int p = 0; p < pad; p++) SerialWriteChar(port, ' ');
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
                SerialITOA((uint64_t)val, buf, 10);

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

                if (!flag_left) for (int p = 0; p < pad; p++) SerialWriteChar(port, pc);
                if (sign) SerialWriteChar(port, sign);
                for (int p = 0; buf[p]; p++) SerialWriteChar(port, buf[p]);
                if (flag_left) for (int p = 0; p < pad; p++) SerialWriteChar(port, ' ');
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
                SerialITOA(val, buf, 10);
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
                SerialITOA(val, buf, 16);

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

                if (!flag_left) for (int p = 0; p < pad; p++) SerialWriteChar(port, pc);
                if (flag_hash && val != 0) { SerialWriteChar(port, '0'); SerialWriteChar(port, spec == 'X' ? 'X' : 'x'); }
                for (int p = 0; buf[p]; p++) SerialWriteChar(port, buf[p]);
                if (flag_left)  for (int p = 0; p < pad; p++) SerialWriteChar(port, ' ');
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
                SerialITOA(val, buf, 8);
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
                SerialWriteChar(port, '0'); SerialWriteChar(port, 'x');
                char buf[32];
                SerialITOA(val, buf, 16);
                for (int p = 0; buf[p]; p++) SerialWriteChar(port, buf[p]);
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
                    for (int p = 0; p < width - 1; p++) SerialWriteChar(port, ' ');
                }
                SerialWriteChar(port, c);
                if (width > 1 && flag_left)
                {
                    for (int p = 0; p < width - 1; p++) SerialWriteChar(port, ' ');
                }
            } break;

            // %%
            case '%':
            {
                SerialWriteChar(port, '%');
            } break;

            // Unknwon: pass through
            default:
            {
                SerialWriteChar(port, '%');
                SerialWriteChar(port, spec);
                break;
            }
        }
    }

    va_end(args);
}