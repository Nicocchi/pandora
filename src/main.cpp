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
#include "fs/ata.h"
#include "fs/fat32.h"
#include "fs/bin_loader.h"
#include "syscall.h"

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

struct Terminal
{
    char input_buffer[256];

    uint32_t input_length;

    uint32_t prompt_col;
    uint32_t prompt_row;
};

Terminal g_terminal = {};
void TerminalBackspace()
{
    if (g_terminal.input_length == 0) return;

    g_terminal.input_length--;
    g_terminal.input_buffer[g_terminal.input_length] = '\0';


    if (kRenderer.cursor_row < g_terminal.prompt_row) return;
    if (kRenderer.cursor_row == g_terminal.prompt_row &&
        kRenderer.cursor_col <= g_terminal.prompt_col) return;

    DrawChar('\b');
    FlushConsole();
}

FatVolume* volumes[ATA_MAX_DRIVES];



static bool StrEq(const char* a, const char* b)
{
    while (*a && *b)
    {
        if (*a != *b) return false;
        a++; b++;
    }
    return *a == *b;
}

// Skip leading spaces, return pointer to first non-space char
static const char* SkipSpaces(const char* s)
{
    while (*s == ' ') s++;
    return s;
}

// Extract first token into out_buf, return pointer to remainder of string
static const char* ParseToken(const char* s, char* out_buf, size_t out_size)
{
    s = SkipSpaces(s);
    size_t i = 0;
    while (*s && *s != ' ' && i < out_size - 1)
    {
        out_buf[i++] = *s++;
    }
    out_buf[i] = '\0';
    return s; // points at remainder (arg)
}

struct LSArgs
{
    char path[256];
    Task* waiter;
};

static void TaskLS(void* arg)
{
    LSArgs* args = (LSArgs*)arg;
    const char* path = (args && args->path[0]) ? args->path : "/";
    FatListDir(volumes[0], path);

    Task* waiter = args->waiter;
    kfree(args);

    if (waiter) scheduler.Wake(waiter);
}

static void DispatchCommand(const char* input)
{
    char cmd[64];
    const char* rest = ParseToken(input, cmd, sizeof(cmd));

    if (cmd[0] == '\0')
    {
        return;
    }
    else if (StrEq(cmd, "ls"))
    {
        LSArgs* args = (LSArgs*)kmalloc(sizeof(LSArgs));
        const char* path_arg = SkipSpaces(rest);

        if (path_arg[0] != '\0')
        {
            // Copy provided path
            size_t i = 0;
            while (path_arg[i] && i < sizeof(args->path) - 1)
            {
                args->path[i] = path_arg[i];
                i++;
            }
            args->path[i] = '\0';
        }
        else
        {
            // Default to root
            args->path[0] = '/';
            args->path[1] = '\0';
        }

        args->waiter = scheduler.current;
        KThreadCreate("ls", TaskLS, args, TaskPriority::Normal);
        scheduler.Block(scheduler.current);
    }
    else
    {
        kprintf("Unknown command: %s\n", cmd);
    }
}

// NOTE (Nico): Temporary initial task
static void KTerminal(void*)
{
    kprintf("\nWelcome to Pandora\n");
    kprintf("> ");
    g_terminal.prompt_col = kRenderer.cursor_col;
    g_terminal.prompt_row = kRenderer.cursor_row;
    
    while (true)
    {
        KeyEvent event;
        if (PopKeyEvent(&event))
        {
            if (event.pressed && event.ascii != '\0'
                && event.pressed && event.ascii != '\n'
                && event.keycode != KeyCode::Backspace 
                && event.keycode != KeyCode::Enter)
            {
                if (g_terminal.input_length < sizeof(g_terminal.input_buffer) - 1)
                {
                    g_terminal.input_buffer[g_terminal.input_length++] = event.ascii;
                    g_terminal.input_buffer[g_terminal.input_length] = '\0';
    
                    kprintf("%c", event.ascii);
                }
            }

            if (event.pressed && event.keycode == KeyCode::Backspace)
            {
                TerminalBackspace();
            }

            if (event.pressed && event.keycode == KeyCode::Enter)
            {
                kprintf("\n");

                DispatchCommand(g_terminal.input_buffer);

                g_terminal.input_length = 0;
                g_terminal.input_buffer[0] = '\0';

                kprintf("> ");

                g_terminal.prompt_col = kRenderer.cursor_col;
                g_terminal.prompt_row = kRenderer.cursor_row;
            }

            
        }
    }
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
        PanicContext ctx = {};
        ctx.message = "revision not supported";
        KernelPanic(ctx);
    }

    if (module_request.response == NULL)
    {
        PanicContext ctx = {};
        ctx.message = "module null";
        KernelPanic(ctx);
    }

    // Ensure there is a framebuffer
    if (framebuffer_request.response == NULL || 
        framebuffer_request.response->framebuffer_count < 1)
    {
        PanicContext ctx = {};
        ctx.message = "framebuffer null";
        KernelPanic(ctx);
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
            PanicContext ctx = {};
            ctx.message = "font file null";
            KernelPanic(ctx);
        }

        font.psf1_Header = (struct PSF1_Header*)file->address;
        if (font.psf1_Header->magic[0] != 0x36 || font.psf1_Header->magic[1] != 0x04)
        {
            PanicContext ctx = {};
            ctx.message = "invalid font magic";
            KernelPanic(ctx);
        }

        font.glyphBuffer = (void*)((uint64_t)file->address + sizeof(PSF1_Header));
    }

    // Setup kernel renderer
    kRenderer.color = WHITE;
    kRenderer.bg_color = BLACK;
    kRenderer.cursor_col = 0;
    kRenderer.cursor_row = 0;
    kRenderer.font = font;
    kRenderer.framebuffer.address = framebuffer->address;
    kRenderer.framebuffer.width = framebuffer->width;
    kRenderer.framebuffer.height = framebuffer->height;
    kRenderer.framebuffer.pitch = framebuffer->pitch;
    kRenderer.framebuffer.pixelsPerScanLine = framebuffer->pitch / 4;

    ClearScreen(BLACK, true);

    struct limine_memmap_response *memmap_response = memmap_request.response;
    if (memmap_response == NULL)
    {
        PanicContext ctx = {};
        ctx.message = "limine_memmap_request is null";
        KernelPanic(ctx);
    }

    if (hhdm_request.response == nullptr)
    {
        PanicContext ctx = {};
        ctx.message = "hhdm null";
        KernelPanic(ctx);
    }

    g_hhdm_offset = hhdm_request.response->offset;

    if (exe_addr_request.response == nullptr)
    {
        PanicContext ctx = {};
        ctx.message = "exe_addr_request null";
        KernelPanic(ctx);
    }


    g_pmm.Init(memmap_response);
    kprintf("[OK] PMM Initialized\n");

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


    uint64_t total_bytes = g_pmm.total_pages * PAGE_SIZE;
    uint64_t free_bytes = g_pmm.free_pages * PAGE_SIZE;
    uint64_t used_bytes = total_bytes - free_bytes;

    kprintf("[PMM] Total : %llu MiB (*%llu pages)\n", total_bytes >> 20, g_pmm.total_pages);
    kprintf("[PMM] Used : %llu MiB (*%llu pages)\n", used_bytes >> 20, total_bytes / PAGE_SIZE - g_pmm.free_pages);
    kprintf("[PMM] Free : %llu MiB (*%llu pages)\n", free_bytes >> 20, g_pmm.free_pages);
    kernelHeap.Init();

    ATAInit();

    int volume_count = 0;
    for (int i = 0; i < g_ata_drive_count && volume_count < ATA_MAX_DRIVES; i++)
    {
        ATADrive* drive = &g_ata_drives[i];
        if (!drive->present) continue;

        // Read MBR
        uint8_t mbr_buf[512];
        if (drive->Read(0, 1, mbr_buf) != ATAResult::OK)
        {
            kprintf("[DISK%d] MBR read failed\n", i);
            continue;
        }
        MBR* mbr = (MBR*)mbr_buf;

        if (mbr->signature != 0xAA55)
        {
            kprintf("[DISK%d] No valid MBR\n", i);
            continue;
        }

        for (int p = 0; p < 4 && volume_count < ATA_MAX_DRIVES; p++)
        {
            auto& part = mbr->partitions[p];
            if (part.type == 0x00) continue; // empty slot
            if (part.type == 0xEE) continue; // GPT protective entry - no real FS here

            kprintf("[DISK%d] Partition %d: type=0x%02x lba=%u\n", i, p, part.type, part.lba_start);

            // Mount from the partition's ACTUAL start LBA on THIS drive. The old
            // code indexed g_ata_drives[] by the partition slot and hardcoded LBA
            // 2048, which only happened to line up on QEMU's single-disk layout.
            FsType fs = DetectFilesystem(drive, part.lba_start);

            if (fs == FsType::FAT32)
            {
                FatVolume* v = FatMount(drive, part.lba_start);
                if (v) volumes[volume_count++] = v;
            }
        }
    }


    InitGDT();
    kprintf("[OK] GDT initialized\n");
    InitIDT();
    kprintf("[OK] IDT initialized\n");
    InitAPIC();
    kprintf("[OK] APIC initialized\n");

    SyscallInit();

    scheduler.Init();
    kprintf("[OK] Scheduler initialized\n");

    // Boot the userspace terminal from the FAT32 disk when an ATA HDD is
    // present (QEMU -drive file=pandora.hdd). When booting from ISO there is
    // no legacy ATA disk, so fall back to Limine modules listed in limine.conf.
    Process* proc = nullptr;
    if (volumes[0])
    {
        proc = LoadBinary(volumes[0], "/bin/terminal.bin");
    }
    if (!proc)
    {
        PanicContext ctx = {};
        ctx.message = "Failed to load terminal.bin (no FAT volume)";
        KernelPanic(ctx);
    }
    Task* t = UThreadCreate(proc, USER_LOAD_BASE, USER_STACK_TOP, TaskPriority::Normal);

    // KTerminal runs at High priority and drains the shared keyboard queue in
    // a busy loop — leave it disabled while testing the userspace terminal.
    // KThreadCreate("KernalTaskA", KTerminal, nullptr, TaskPriority::High);
    kprintf("[OK] Tasks created\n");
    
    // // Init IRQs
    kprintf("Initializing IRQs...\n");
    InitPit();
    kprintf("[OK] Pit initialized\n");
    InitKeyboard();
    
    kprintf("[OK] Keyboard initialized\n");
    
    EnableInterrupts();
    kprintf("[OK] Interrupts enabled, scheduler running\n");

    hcf();
}
