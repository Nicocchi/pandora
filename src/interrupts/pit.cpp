#include "pit.h"
#include "idt.h"
#include "common.h"
#include "processes/scheduler.h"
#include "drivers/serial_port.h"

static uint64_t ticks = 0;
static uint32_t frequency = 100;

static void PITHandler(InterruptFrame* frame)
{
    (void)frame;
    ticks++;
    scheduler.Tick();
}

void InitPit()
{
    uint16_t divisor = 1193180 / frequency;
    
    OutPortB(0x43, 0x36);
    
    OutPortB(0x40, divisor & 0xFF);
    OutPortB(0x40, (divisor >> 8) & 0xFF);
    
    RegisterInterruptHandler(32, PITHandler);
}
