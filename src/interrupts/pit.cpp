#include "pit.h"
#include "idt.h"
#include "common.h"
#include "boot/limine_vga.h"
#include "drivers/serial_port.h"

static uint64_t ticks = 0;

static void PITHandler(InterruptFrame* frame)
{
    (void)frame;
    ticks++;
}

void InitPit(uint32_t frequency)
{
    uint16_t divisor = 1193180 / frequency;
    
    OutPortB(0x43, 0x36);
    
    OutPortB(0x40, divisor & 0xFF);
    OutPortB(0x40, (divisor >> 8) & 0xFF);
    
    RegisterInterruptHandler(32, PITHandler);
}
