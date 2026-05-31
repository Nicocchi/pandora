/**
 * @file        main.cpp
 * @brief       Core kernel initialization engine and execution entry point.
 * 
 * @copyright   Copyright (c) 2026, Nicocchi
 * @license     Licensed under the GPL2 License
 * 
 * @author      Nicocchi
 * @date        May 28, 2026
 * 
 * @details     Orchestrates early bare-metal platform bootstrapping over the 
 *              Limine Boot Protocol. Manages the parsing of hardware tracking structs, 
 *              initializes primitive debugging communication subsystems, bootstraps C++ 
 *              global runtimes, structures font tables, and passes control off to 
 *              the internal terminal environment.
 */

#include <stdint.h>
#include <stddef.h>

#include <boot/limine_headers.h>
#include <boot/limine_vga.h>
#include <drivers/serial_port.h>
#include "gdt.h"
#include "interrupts/idt.h"
#include "interrupts/pit.h"
#include "interrupts/apic.h"
#include "common.h"
#include "drivers/ps2_keyboard.h"

/**
 * @name        C++ Initialization Pointers
 * @brief       Linker script markers indicating the physical memory bounds of global constructor tables.
 * @{
 */
extern "C" {
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
}
/** @} */

/**
 * @brief       Executes initialization loops across early C++ static and global object constructors.
 * @details     Iterates through the `.init_array` section populated by the compiler toolchain, 
 *              safely calling each constructor function pointer sequentially before `kmain` triggers 
 *              object interactions.
 */
static void CallGlobalConstructors()
{
    for (auto ctor = __init_array_start; ctor < __init_array_end; ctor++)
        (*ctor)();
}


/**
 * @brief       Main entry threshold for kernel runtime operations (called natively by Limine).
 * @details     Performs early structural checks on bootloader response elements, initialises 
 *              the primary 16550 UART serial connection, maps direct font rendering buffers, 
 *              and draws text output routines onto the linear graphical display workspace.
 */
extern "C" void kmain(void)
{
    if (SerialInit(COM1_PORT) != 0)
    {
        // Force-disable loopback
        __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x0F), "Nd"((uint16_t)(COM1_PORT + 4)));
        SerialWriteString(COM1_PORT, "Kernel Panic: Serial Initialization Loopback Test Failed!\n");
        hcf();
    }
    
    CallGlobalConstructors();
    SerialWriteString(COM1_PORT, "CallGlobalConstructors set\n");
    
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        KernelPanic("revision not supported\n");
    }
    SerialWriteString(COM1_PORT, "revision OK\n");

    if (module_request.response == NULL)
    {
        KernelPanic("module NULL\n");
    }
    SerialWriteString(COM1_PORT, "module OK\n");

    // Ensure there is a framebuffer
    if (framebuffer_request.response == NULL || 
        framebuffer_request.response->framebuffer_count < 1)
    {
        KernelPanic("framebuffer NULL");
    }
    SerialWriteString(COM1_PORT, "framebuffer OK\n");

    // Fetch the first framebuffer
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];

    // Create a PSF console font
    PSF1_Font font;
    {
        const char *fName = "zap-light16.psf";
        struct limine_file *file = GetFileLimine(fName);

        if (file == NULL)
        {
            KernelPanic("font file NULL\n");
        }
        SerialWriteString(COM1_PORT, "font file OK\n");

        font.psf1_Header = (struct PSF1_Header*)file->address;
        if (font.psf1_Header->magic[0] != 0x36 || font.psf1_Header->magic[1] != 0x04)
        {
            KernelPanic("invalid font magic\n");
        }
        SerialWriteString(COM1_PORT, "font magic OK\n");

        font.glyphBuffer = (void*)((uint64_t)file->address + sizeof(PSF1_Header));
        SerialWriteString(COM1_PORT, "glyph buffer assigned\n");
    }

    // Setup kernel renderer
    kRenderer.color = WHITE;
    kRenderer.x = 0;
    kRenderer.y = 0;
    kRenderer.font = font;
    kRenderer.framebuffer.address = framebuffer->address;
    kRenderer.framebuffer.width = framebuffer->width;
    kRenderer.framebuffer.height = framebuffer->height;
    kRenderer.framebuffer.pitch = framebuffer->pitch;
    kRenderer.framebuffer.pixelsPerScanLine = framebuffer->pitch / 4;
    SerialWriteString(COM1_PORT, "kernel renderer set\n");
    SerialWriteString(COM1_PORT, "kernel renderer set2\n");

    SerialWriteString(COM1_PORT, " \n");

    ClearScreen(BLACK, true);

    InitGDT();
    kprintf("[OK] GDT initialized\n");
    InitIDT();
    kprintf("[OK] IDT initialized\n");
    InitAPIC();
    DisablePIC();
    InitLAPIC();
    InitIOAPIC();

    kprintf("[OK] APIC initialized\n");

    // Init IRQs
    kprintf("[OK] Initializing IRQs\n");
    InitPit();
    kprintf("[OK] Pit initialized\n");
    InitKeyboard();

    kprintf("[OK] Keyboard initialized\n");

    EnableInterrupts();
    kprintf("\n");
    while (true)
    {
        KeyEvent event;
        if (PopKeyEvent(&event))
        {
            if (event.pressed && event.ascii != '\0')
            {
                kprintf("%c", event.ascii);
            }
        }
    }


    hcf();
}
