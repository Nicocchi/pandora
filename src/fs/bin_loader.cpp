#include "bin_loader.h"
#include "memory/vmm.h"
#include "memory/pmm.h"
#include "memory/heap.h"
#include "common.h"
#include "boot/limine.h"
#include "lib/string.h"

extern volatile struct limine_module_request module_request;

extern "C" struct limine_file* GetFileLimine(const char* name)
{
    struct limine_module_response* module_response = module_request.response;
    if (!module_response) return nullptr;

    for (size_t i = 0; i < module_response->module_count; i++)
    {
        struct limine_file* f = module_response->modules[i];
        if (f && f->path && CheckStringEndsWith(f->path, name))
            return f;
    }
    return nullptr;
}

static uint64_t AllocAndMap(AddressSpace* as, uint64_t virt_base, uint64_t page_count, uint64_t flags)
{
    // Find the smallest PMM order that covers page_count pages
    uint8_t order = 0;
    while ((1u << order) < page_count) order++;

    uint64_t phys = PMMAlloc(&g_pmm, order);
    if (!phys) return 0;

    // Zero the pages through the HHDM before handing them to userspace
    uint8_t* virt_alias = (uint8_t*)PhysToVirt(phys);
    uint64_t byte_count = (uint64_t)(1u << order) * PAGE_SIZE;
    for (uint64_t i = 0; i < byte_count; i++) virt_alias[i] = 0;

    // Map each page individually into the user address space
    uint64_t mapped = 0;
    for (uint64_t i = 0; i < page_count; i++)
    {
        uint64_t v = virt_base + i * PAGE_SIZE;
        uint64_t p = phys + i * PAGE_SIZE;
        if (!as->MapPage(v, p, flags))
        {
            // Unmap what was already mapped then free the allocation
            for (uint64_t j = 0; j < mapped; j++) as->UnmapPage(virt_base + j * PAGE_SIZE);
            PMMFree(&g_pmm, phys, order);
            return 0;
        }
        mapped++;
    }
    return phys;
}

bool LoadBinaryInto(AddressSpace* as, FatVolume* vol, const char* path)
{
    FatFile* file = FatOpen(vol, path);
    if (!file)
    {
        // kprintf("[loader] LoadBinaryInto: cannot open '%s'\n", path);
        return false;
    }

    uint32_t file_size = file->file_size;
    if (file_size == 0)
    {
        // kprintf("[loader] LoadBinaryInto: '%s' is empty\n", path);
        FatClose(file);
        return false;
    }

    // Map code pages one at a time (order-0), reading the file straight into
    // each fresh frame through its HHDM alias, then mapping it RX.
    uint64_t code_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t remaining = file_size;
    for (uint64_t i = 0; i < code_pages; i++)
    {
        uint64_t frame = PMMAlloc(&g_pmm, 0);
        if (!frame) { FatClose(file); return false; }

        uint8_t* alias = (uint8_t*)PhysToVirt(frame);
        for (uint64_t b = 0; b < PAGE_SIZE; b++) alias[b] = 0;

        uint64_t want = remaining < PAGE_SIZE ? remaining : PAGE_SIZE;
        if (want)
        {
            size_t got = FatRead(file, alias, want);
            if (got != want)
            {
                // kprintf("[loader] LoadBinaryInto: short read on '%s'\n", path);
                PMMFree(&g_pmm, frame, 0);
                FatClose(file);
                return false;
            }
            remaining -= want;
        }

        if (!as->MapPage(USER_LOAD_BASE + i * PAGE_SIZE, frame, VMM_FLAGS_USER_RX))
        {
            PMMFree(&g_pmm, frame, 0);
            FatClose(file);
            return false;
        }
    }
    FatClose(file);

    // Map the RW/NX user stack, one zeroed order-0 page at a time.
    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint64_t i = 0; i < stack_pages; i++)
    {
        uint64_t frame = PMMAlloc(&g_pmm, 0);
        if (!frame) return false;

        uint8_t* alias = (uint8_t*)PhysToVirt(frame);
        for (uint64_t b = 0; b < PAGE_SIZE; b++) alias[b] = 0;

        if (!as->MapPage(stack_base + i * PAGE_SIZE, frame, VMM_FLAGS_USER_RW))
        {
            PMMFree(&g_pmm, frame, 0);
            return false;
        }
    }

    return true;
}

bool LoadBinaryIntoFromMemory(AddressSpace* as, const void* data, uint32_t size)
{
    if (!data || size == 0) return false;

    const uint8_t* src = (const uint8_t*)data;
    uint64_t code_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t remaining = size;

    for (uint64_t i = 0; i < code_pages; i++)
    {
        uint64_t frame = PMMAlloc(&g_pmm, 0);
        if (!frame) return false;

        uint8_t* alias = (uint8_t*)PhysToVirt(frame);
        for (uint64_t b = 0; b < PAGE_SIZE; b++) alias[b] = 0;

        uint64_t want = remaining < PAGE_SIZE ? remaining : PAGE_SIZE;
        if (want)
        {
            for (uint64_t b = 0; b < want; b++) alias[b] = src[b];
            src += want;
            remaining -= want;
        }

        if (!as->MapPage(USER_LOAD_BASE + i * PAGE_SIZE, frame, VMM_FLAGS_USER_RX))
        {
            PMMFree(&g_pmm, frame, 0);
            return false;
        }
    }

    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint64_t i = 0; i < stack_pages; i++)
    {
        uint64_t frame = PMMAlloc(&g_pmm, 0);
        if (!frame) return false;

        uint8_t* alias = (uint8_t*)PhysToVirt(frame);
        for (uint64_t b = 0; b < PAGE_SIZE; b++) alias[b] = 0;

        if (!as->MapPage(stack_base + i * PAGE_SIZE, frame, VMM_FLAGS_USER_RW))
        {
            PMMFree(&g_pmm, frame, 0);
            return false;
        }
    }

    return true;
}

Process* LoadBinaryFromMemory(const void* data, uint32_t size)
{
    if (!data || size == 0) return nullptr;

    Process* proc = Process::CreateUserProcess();
    if (!proc) return nullptr;

    AddressSpace* as = &proc->address_space;

    uint64_t code_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t code_phys = AllocAndMap(as, USER_LOAD_BASE, code_pages, VMM_FLAGS_USER_RX);
    if (!code_phys)
    {
        proc->Destroy();
        return nullptr;
    }

    uint8_t* load_dest = (uint8_t*)PhysToVirt(code_phys);
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < size; i++) load_dest[i] = src[i];

    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t stack_phys = AllocAndMap(as, stack_base, stack_pages, VMM_FLAGS_USER_RW);
    if (!stack_phys)
    {
        as->UnmapRange(USER_LOAD_BASE, code_pages * PAGE_SIZE);
        uint8_t order = 0;
        while ((1u << order) < code_pages) order++;
        PMMFree(&g_pmm, code_phys, order);
        proc->Destroy();
        return nullptr;
    }

    return proc;
}

Process* LoadBinary(FatVolume* vol, const char* path)
{
    // Open the file and validate it
    FatFile* file = FatOpen(vol, path);
    if (!file)
    {
        // kprintf("[loader] LoadBinary: cannot open '%s'\n", path);
        return nullptr;
    }

    uint32_t file_size = file->file_size;
    if (file_size == 0)
    {
        // kprintf("[loader] LoadBinary: '%s' is empty\n", path);
        FatClose(file);
        return nullptr;
    }

    // Create the user process (forks kernel upper-half PML4 entries)
    Process* proc = Process::CreateUserProcess();
    if (!proc)
    {
        // kprintf("[loader] LoadBinary: failed to create process\n");
        FatClose(file);
        return nullptr;
    }

    AddressSpace* as = &proc->address_space;

    // Map code pages (RX, no write, no NX)
    // Need 4KiB pages to hold the entire binary.
    // AllocAndMap rounds up to a PMM power-of-two order internally, but
    // it only needs to map `code_pages` of them into the address space so the
    // padding tail stays inaccessible from userspace
    uint64_t code_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t code_phys = AllocAndMap(as, USER_LOAD_BASE, code_pages, VMM_FLAGS_USER_RX);

    if (!code_phys)
    {
        // kprintf("[loader] LoadBinary: failed to map code pages\n");
        proc->Destroy();
        FatClose(file);
        return nullptr;
    }

    // Read the binary into the freshly mapped pages
    // Can't write through the user mapping (it's RX), so use the
    // HHDM alias of the same physical pages to do the load, then the
    // user mapping becomes live automatically
    uint8_t* load_dest = (uint8_t*)PhysToVirt(code_phys);
    size_t bytes_read = FatRead(file, load_dest, file_size);
    FatClose(file);

    if (bytes_read != file_size)
    {
        // kprintf("[loader] LoadBinary: short read (%zu / %u bytes)\n", bytes_read, file_size);
        // Unmap and free code pages
        as->UnmapRange(USER_LOAD_BASE, code_pages * PAGE_SIZE);
        uint8_t order = 0;
        while ((1u << order) < code_pages) order++;
        PMMFree(&g_pmm, code_phys, order);
        proc->Destroy();
        return nullptr;
    }

    // Map the user stack (RW + NX, not executable)
    // Stack VA: (USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP]
    // RSP will be set to USER_STACK_TOP (the top, before any pushes)
    uint64_t stack_pages = USER_STACK_SIZE / PAGE_SIZE;
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t stack_phys = AllocAndMap(as, stack_base, stack_pages, VMM_FLAGS_USER_RW);
    if (!stack_phys)
    {
        // kprintf("[loader] LoadBinary: failed to map user stack\n");
        as->UnmapRange(USER_LOAD_BASE, code_pages * PAGE_SIZE);
        uint8_t order = 0;
        while ((1u << order) < code_pages) order++;
        PMMFree(&g_pmm, code_phys, order);
        proc->Destroy();
        return nullptr;
    }

    // Return the ready process to the caller
    // Caller does:
    //  Task* t = UThreadCreate(proc, USER_LOAD_BASE, USER_STACK_TOP, priority);
    // kprintf("[loader] Loaded '%s': %u bytes at 0x%llx, stack at 0x%llx\n",
    //         path, file_size, USER_LOAD_BASE, USER_STACK_TOP);
    return proc;
}