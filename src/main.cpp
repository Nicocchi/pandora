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
#include "memory/pmm.h"
#include "memory/vmm.h"
#include "memory/heap.h"
#include "gdt.h"
#include "interrupts/idt.h"
#include "interrupts/pit.h"
#include "interrupts/apic.h"
#include <drivers/serial_port.h>
#include "drivers/ps2_keyboard.h"
#include "lib/stdio.h"
#include "common.h"
#include "processes/scheduler.h"

extern BuddyAllocator g_pmm;
extern VirtualMemoryManager virtualMemoryManager;

extern volatile struct limine_executable_address_request exe_addr_request;

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

uint64_t g_hhdm_offset = 0;

// Test task functions
static void TaskA(void*)
{
    for (int i = 0; i < 5; i++)
    {
        kprintf("[TaskA] tick %d\n", i);
        KSleep(100); // sleep ~1 second (100 ticks at 100Hz)
    }
    kprintf("[TaskA] done\n");
}
static void TaskB(void*)
{
    for (int i = 0; i < 5; i++)
    {
        kprintf("[TaskB] tick %d\n", i);
        KSleep(150); // sleep ~1.5 second
    }
    kprintf("[TaskB] done\n");
}
static void TaskC(void*)
{
    // High priority - should preempt A and B regularly
    for (int i = 0; i < 8; i++)
    {
        kprintf("[TaskC-HIGH] tick %d\n", i);
        KSleep(50);
    }
    kprintf("[TaskC] done\n");
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
    
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false)
    {
        KernelPanic("revision not supported\n");
    }

    if (module_request.response == NULL)
    {
        KernelPanic("module NULL\n");
    }

    // Ensure there is a framebuffer
    if (framebuffer_request.response == NULL || 
        framebuffer_request.response->framebuffer_count < 1)
    {
        KernelPanic("framebuffer NULL");
    }

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

        font.psf1_Header = (struct PSF1_Header*)file->address;
        if (font.psf1_Header->magic[0] != 0x36 || font.psf1_Header->magic[1] != 0x04)
        {
            KernelPanic("invalid font magic\n");
        }

        font.glyphBuffer = (void*)((uint64_t)file->address + sizeof(PSF1_Header));
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

    ClearScreen(BLUE, true);

    struct limine_memmap_response *memmap_response = memmap_request.response;
    if (memmap_response == NULL)
    {
        KernelPanic("LIMINE_MEMMAP_REQUEST NULL\n");
    }

    if (hhdm_request.response == nullptr)
    {
        KernelPanic("HHDM not found\n");
    }

    g_hhdm_offset = hhdm_request.response->offset;

    if (exe_addr_request.response == nullptr)
    {
        KernelPanic("EXE_ADDR_REQUEST not found\n");
    }


    g_pmm.Init(memmap_response);
    kprintf("[OK] PMM Initialized\n");
    // {
    //     kprintf("[OK] PMM Memory testing...\n");
    
    //     // Allocate a single page
    //     uintptr_t page = PMMAlloc(&g_pmm, 0);
    //     kprintf("[PMM] Allocated a single page...\n");
    
    //     // Allocate 8 contiguous pages (order 3 = 2^3)
    //     uintptr_t block = PMMAlloc(&g_pmm, 3);
    //     kprintf("[PMM] Allocated 8 contiguous pages...\n");
    
    //     // Free them
    //     PMMFree(&g_pmm, page, 0);
    //     PMMFree(&g_pmm, block, 3);
    //     kprintf("[PMM] Free memory...\n");
    // }

    uint64_t kernel_phys = exe_addr_request.response->physical_base;
    uint64_t kernel_virt = exe_addr_request.response->virtual_base;

    // Calculate kernel size from linker symbols
    extern char _kernel_start[], _kernel_end[];
    uint64_t kernel_size = (uint64_t)_kernel_end - (uint64_t)_kernel_start;

    kprintf("[VMM] initializing (inherit Limine HHDM)\n");
    uint64_t rsp_val;
    asm volatile("mov %%rsp, %0" : "=r"(rsp_val));
    
    virtualMemoryManager.Init(kernel_phys, kernel_virt, kernel_size);
    kprintf("[OK] Virtual Memory allocated\n");
    // {
    //     kprintf("[OK] VMM Memory testing...\n");
    //     // Allocate a physical page and map it to an arbitrary kernel virtual addr
    //     uint64_t test_phys = PMMAlloc(&g_pmm, 0);
    //     uint64_t test_virt = 0xFFFF900000000000ULL;  // arbitrary kernel virtual

    //     bool mapped = virtualMemoryManager.MapPage(test_virt, test_phys, VMM_FLAGS_KERNEL_RW);
    //     kprintf("[VMM] MapPage: %s\n", mapped ? "OK" : "FAILED");

    //     // Write a known value through the virtual address and read it back
    //     volatile uint64_t *ptr = (volatile uint64_t *)test_virt;
    //     *ptr = 0xDEADBEEFCAFEBABEULL;
    //     uint64_t readback = *ptr;
    //     kprintf("[VMM] Write/read test: %s (0x%llx)\n",
    //         readback == 0xDEADBEEFCAFEBABEULL ? "OK" : "FAILED", readback);

    //     // Verify Translate returns the right physical address
    //     uint64_t translated = virtualMemoryManager.Translate(test_virt);
    //     kprintf("[VMM] Translate: 0x%llx -> 0x%llx %s\n",
    //         test_virt, translated,
    //         (translated & PTE_ADDR_MASK) == test_phys ? "OK" : "FAILED");

    //     // Unmap and free
    //     virtualMemoryManager.UnmapPage(test_virt);
    //     PMMFree(&g_pmm, test_phys, 0);
    //     kprintf("[VMM] Unmap: OK\n");
    // }

    
    // kprintf("[OK] Memory test done\n");


    uint64_t total_bytes = g_pmm.total_pages * PAGE_SIZE;
    uint64_t free_bytes = g_pmm.free_pages * PAGE_SIZE;
    uint64_t used_bytes = total_bytes - free_bytes;

    kprintf("[PMM] Total : %llu MiB (*%llu pages)\n", total_bytes >> 20, g_pmm.total_pages);
    kprintf("[PMM] Used : %llu MiB (*%llu pages)\n", used_bytes >> 20, total_bytes / PAGE_SIZE - g_pmm.free_pages);
    kprintf("[PMM] Free : %llu MiB (*%llu pages)\n", free_bytes >> 20, g_pmm.free_pages);
    SerialWriteString(COM1_PORT, "[PMM] Total : %llu MiB (*%llu pages)\n", total_bytes >> 20, g_pmm.total_pages);
    SerialWriteString(COM1_PORT, "[PMM] Used : %llu MiB (*%llu pages)\n", used_bytes >> 20, total_bytes / PAGE_SIZE - g_pmm.free_pages);
    SerialWriteString(COM1_PORT, "[PMM] Free : %llu MiB (*%llu pages)\n", free_bytes >> 20, g_pmm.free_pages);
    kernelHeap.Init();

    

    InitGDT();
    kprintf("[OK] GDT initialized\n");
    InitIDT();
    kprintf("[OK] IDT initialized\n");
    InitAPIC();
    DisablePIC();
    InitLAPIC();
    InitIOAPIC();
    kprintf("[OK] APIC initialized\n");

    scheduler.Init();
    kprintf("[OK] Scheduler initialized\n");

    // // Test tasks
    KThreadCreate("TaskA", TaskA, nullptr, TaskPriority::Normal);
    KThreadCreate("TaskB", TaskB, nullptr, TaskPriority::Normal);
    KThreadCreate("TaskC", TaskC, nullptr, TaskPriority::High);
    kprintf("[OK] Tasks created\n");
    
    // // Init IRQs
    kprintf("Initializing IRQs...\n");
    InitPit();
    kprintf("[OK] Pit initialized\n");
    // InitKeyboard();
    
    // kprintf("[OK] Keyboard initialized\n");
    
    EnableInterrupts();
    kprintf("[OK] Interrupts enabled, scheduler running\n");
    
    // kprintf("\n");

    // while (true)
    // {
    //     KeyEvent event;
    //     if (PopKeyEvent(&event))
    //     {
    //         if (event.pressed && event.ascii != '\0')
    //         {
    //             kprintf("%c", event.ascii);
    //         }
    //     }
    // }


    while (true)
    {
        asm volatile("hlt");
    }
}
