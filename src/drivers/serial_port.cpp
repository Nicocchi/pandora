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

// void SerialWriteString(uint16_t port, const char* str)
// {
//     if (!str) return;
//     for (size_t i = 0; str[i] != '\0'; i++)
//     {
//         SerialWriteChar(port, str[i]);
//     }
// }

void SerialWriteString(uint16_t port, const char *fmt, ...)
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
                        SerialWriteChar(port, *s++);
                    }
                } break;

                case 'd':
                {
                    int64_t d = va_arg(args, int);
                    char buf[32];
                    if (d < 0)
                    {
                        SerialWriteChar(port, '-');
                        d = -d;
                    }
                    SerialITOA(d, buf, 10);
                    for (size_t j = 0; buf[j] != '\0'; j++) SerialWriteChar(port, buf[j]);
                } break;

                case 'x':
                {
                    // 32-bit hex
                    uint32_t x = va_arg(args, uint32_t);
                    char buf[32];
                    SerialITOA(x, buf, 16);
                    for (size_t j = 0; buf[j] != '\0'; j++) SerialWriteChar(port, buf[j]);
                } break;
                case 'l':
                {
                    // look ahead for llx
                    if (fmt[i+1] == 'l' && fmt[i+2] == 'x')
                    {
                        i += 2;

                        uint64_t val = va_arg(args, uint64_t);

                        char buf[32];
                        SerialITOA(val, buf, 16);

                        for (size_t j = 0; buf[j]; j++) SerialWriteChar(port, buf[j]);
                    }
                } break;

                case 'p':
                {
                    uint64_t ptr = (uint64_t)va_arg(args, void*);

                    SerialWriteChar(port, '0');
                    SerialWriteChar(port, 'x');

                    char buf[32];
                    SerialITOA(ptr, buf, 16);

                    for (size_t j = 0; buf[j]; j++) SerialWriteChar(port, buf[j]);
                } break;

                case 'c':
                {
                    char c = (char)va_arg(args, int);
                    SerialWriteChar(port, c);
                } break;

                case '%':
                {
                    SerialWriteChar(port, '%');
                } break;

                default:
                {
                    SerialWriteChar(port, '%');
                    SerialWriteChar(port, fmt[i]);
                } break;
            }
        }
        else {
            SerialWriteChar(port, fmt[i]);
        }
    }

    va_end(args);
}