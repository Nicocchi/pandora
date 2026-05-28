#include <stdint.h>
#include <stddef.h>
#include "serial_port.h"

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

void SerialWriteString(uint16_t port, const char* str)
{
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        SerialWriteChar(port, str[i]);
    }
}