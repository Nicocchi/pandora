/**
 * @file        serial_port.h
 * @brief       Driver for x86 16550 UART Serial Port communication
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 27, 2026
 * 
 * @details     Provides standalone, standard functions to initialize COM1
 *              and transmit data out of the port for debugging and logging.
 *              Based entirely on public domain hardware specifications.
 */

#pragma once

// Standard x86 COM Port I/O Address
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8

/**
 * @brief Initializes the specified serial port to 38400 baud, 8N1, no FIFO
 * 
 * @param port The base I/O port address (e.g., COM1_PORT)
 * @return 0 on sucess, 1 if the port hardware loopback test failed
 */
int SerialInit(uint16_t port);

/**
 * @brief Transmits a single byte/character over the serial port
 * 
 * @param port The base I/O port address
 * @param c The character to send
 */
void SerialWrite(uint16_t port, char c);

/**
 * @brief Transmits a null-terminated string over the serial port
 * 
 * @param port The base I/O port address
 * @param str Pointer to the null-terminated string
 */
void SerialWriteString(uint16_t port, const char* str);