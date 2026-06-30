#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "common.h"
#include "processes/scheduler.h"
#include "lib/stdio.h"
#include "drivers/ps2_keyboard.h"
#include "memory/vmm.h"
#include "memory/pmm.h"
#include "memory/heap.h"
#include "fs/fat32.h"
#include "fs/bin_loader.h"
#include "boot/limine.h"

// Mounted FAT volumes (defined in main.cpp). Index 0 is the boot volume.
extern FatVolume* volumes[];

static const char* PathBaseName(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p; p++)
        if (*p == '/') base = p + 1;
    return base;
}

static void WriteMSR(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)(val & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(val >> 32);
    asm volatile("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

static uint64_t ReadMSR(uint32_t msr)
{
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static bool IsUserPtr(const void* ptr, size_t len)
{
    uint64_t start = (uint64_t)ptr;
    uint64_t end = start + (uint64_t)len;
    return start >= PAGE_SIZE
        && end <= USER_SPACE_END
        && end >= start;
}

// Copy a NUL-terminated string from the current (user) address space into a
// kernel buffer. Returns the length copied (excluding NUL), or -1 on a bad
// pointer / overflow. Reads happen under the caller's CR3 (still active).
static long CopyUserString(const char* user, char* dst, size_t dst_size)
{
    if (!dst_size) return -1;
    for (size_t i = 0; i < dst_size; i++)
    {
        const char* p = user + i;
        if (!IsUserPtr(p, 1)) return -1;
        char c = *(volatile const char*)p;
        dst[i] = c;
        if (c == '\0') return (long)i;
    }
    return -1; // not terminated within dst_size
}

// argv build limits for exec()
static constexpr int EXEC_MAX_ARGS = 16;
static constexpr int EXEC_ARG_LEN = 128;   // per-argument cap (incl NUL)

// Write a value into a freshly-built address space's top stack page through the
// HHDM physical alias. `top_page_uv` is the user VA of the start of that page.
static void PokeStack(AddressSpace* as, uint64_t top_page_uv, uint64_t uv,
                      const void* src, size_t n)
{
    uint64_t frame = as->Translate(top_page_uv);
    uint8_t* alias = (uint8_t*)PhysToVirt(frame);
    uint8_t* dst = alias + (uv - top_page_uv);
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) dst[i] = s[i];
}

extern "C" uint64_t syscall_dispatch(uint64_t nr, uint64_t arg0, uint64_t arg1,
                                     uint64_t arg2, SyscallFrame* frame)
{
    switch(nr)
    {
        // SYS_EXIT (0)
        // arg0 = exit code (ignored for now)
        case SYS_EXIT:
        {
            scheduler.Exit((long)arg0);
            __builtin_unreachable();
        }

        // SYS_WRITE (1)
        // arg0 = fd    (0 = stdout, anything else ignored for now)
        // arg1 = buf ptr (user virtual address)
        // arg2 = length (bytes)
        // Returns: bytes written, or (uint64_t)-1 on error
        case SYS_WRITE:
        {
            uint64_t fd = arg0;
            const char* buf = (const char*)arg1;
            size_t len = (size_t)arg2;

            // Only stdout for now
            if (fd != 0) return (uint64_t)-1;

            // validate the pointer before touching it
            if (!IsUserPtr(buf, len)) return (uint64_t)-1;

            // Cap to a sane limit so a buggy program can't stall the kernel
            if (len > 4096) len = 4096;

            for (size_t i = 0; i < len; i++)
                DrawChar(buf[i]);
            FlushConsole();

            return (uint64_t)len;
        }

        case SYS_READ:
        {
            uint64_t fd = arg0;
            char* buf = (char*)arg1;
            size_t len = (size_t)arg2;

            // Only stdin (fd=0) and keyboard for now
            if (fd != 0) return (uint64_t)-1;
            if (!IsUserPtr(buf, len)) return (uint64_t)-1;
            if (len == 0) return 0;

            // Non-blocking: check once and return.  The userspace caller
            // spins (like KTerminal's while(true) loop) so IRQ delivery and
            // scheduler preemption work normally between attempts.
            //
            // Do NOT poll the controller (port 0x60) here: the keyboard is
            // interrupt-driven (IRQ1 -> vector 33 fills the ring buffer). A
            // poll would race the IRQ handler for the same byte and can
            // re-read a stale scancode, duplicating keypresses ("ls" -> "lss").
            KeyEvent ev;
            if (!PopKeyEvent(&ev))
                return 0;

            if (!ev.pressed)
                return 0;

            char ch = 0;
            if (ev.ascii != '\0')
                ch = ev.ascii;
            else if (ev.keycode == KeyCode::Enter)
                ch = '\n';
            else
                return 0;

            *(volatile char*)buf = ch;
            return 1;
        }

        // SYS_FORK (3)
        // Returns: child pid to the parent, 0 to the child, (uint64_t)-1 on error
        case SYS_FORK:
        {
            Task* child = scheduler.Fork(frame);
            if (!child || !child->process) return (uint64_t)-1;
            return (uint64_t)child->process->pid;
        }

        // SYS_EXEC (4)
        // arg0 = path (user string), arg1 = argv (user char*[], NULL-terminated)
        // Replaces the current process image; does not return on success.
        case SYS_EXEC:
        {
            const char* upath = (const char*)arg0;
            char* const* uargv = (char* const*)arg1;

            // 1. Copy path out of the (still-active) user address space.
            char kpath[256];
            if (CopyUserString(upath, kpath, sizeof(kpath)) < 0)
                return (uint64_t)-1;

            // 2. Copy argv strings into kernel memory.
            static char kargs[EXEC_MAX_ARGS][EXEC_ARG_LEN];
            int argc = 0;
            if (uargv)
            {
                for (; argc < EXEC_MAX_ARGS; argc++)
                {
                    const char* const* slot = uargv + argc;
                    if (!IsUserPtr(slot, sizeof(char*))) return (uint64_t)-1;
                    const char* astr = *(const char* const volatile*)slot;
                    if (!astr) break; // NULL terminator
                    if (CopyUserString(astr, kargs[argc], EXEC_ARG_LEN) < 0)
                        return (uint64_t)-1;
                }
            }

            // 3. Build a fresh address space and load the new image into it.
            AddressSpace new_as;
            if (!new_as.Init(&virtualMemoryManager.kernel_space))
                return (uint64_t)-1;
            bool loaded = false;
            if (volumes[0])
                loaded = LoadBinaryInto(&new_as, volumes[0], kpath);
            if (!loaded)
            {
                struct limine_file* mod = GetFileLimine(PathBaseName(kpath));
                if (mod)
                    loaded = LoadBinaryIntoFromMemory(&new_as,
                        (const void*)mod->address, (uint32_t)mod->size);
            }
            if (!loaded)
            {
                new_as.DestroyUser();
                return (uint64_t)-1;
            }

            // 4. Lay out [argc][argv0..argv(n-1)][NULL] + strings on the top
            //    stack page. Final rsp (argc slot) is kept 16-byte aligned.
            const uint64_t top_page_uv = USER_STACK_TOP - PAGE_SIZE;
            uint64_t cursor = USER_STACK_TOP;
            uint64_t arg_uv[EXEC_MAX_ARGS];

            for (int i = 0; i < argc; i++)
            {
                size_t len = 0;
                while (kargs[i][len]) len++;
                len++; // include NUL
                cursor -= len;
                PokeStack(&new_as, top_page_uv, cursor, kargs[i], len);
                arg_uv[i] = cursor;
            }

            uint64_t needed = 8 /*argc*/ + 8 * (uint64_t)(argc + 1);
            cursor -= needed;
            cursor &= ~((uint64_t)0xF);

            uint64_t argc_val = (uint64_t)argc;
            PokeStack(&new_as, top_page_uv, cursor, &argc_val, 8);
            for (int i = 0; i < argc; i++)
                PokeStack(&new_as, top_page_uv, cursor + 8 + 8 * (uint64_t)i,
                          &arg_uv[i], 8);
            uint64_t nullp = 0;
            PokeStack(&new_as, top_page_uv, cursor + 8 + 8 * (uint64_t)argc,
                      &nullp, 8);

            // 5. Swap in the new address space and free the old one.
            AddressSpace old_as = scheduler.current->process->address_space;
            scheduler.current->process->address_space = new_as;
            new_as.Load();
            old_as.DestroyUser();

            // 6. Rewrite the saved frame so syscall_entry's return path sysrets
            //    into the new program's _start, with argc/argv on the stack.
            frame->rip = USER_LOAD_BASE;
            frame->rsp = cursor;
            frame->rflags = 0x202;
            frame->rdi = frame->rsi = frame->rdx = 0;
            frame->r8 = frame->r9 = frame->r10 = 0;
            frame->rbx = frame->rbp = 0;
            frame->r12 = frame->r13 = frame->r14 = frame->r15 = 0;

            return 0; // value ignored by the freshly-loaded image
        }

        // SYS_WAIT (5)
        // Blocks until the current task's child exits; returns its exit code.
        case SYS_WAIT:
        {
            return (uint64_t)scheduler.Wait();
        }

        // SYS_READDIR (6)
        // arg0 = path (user string)
        // arg1 = FatDirInfo* user buffer
        // arg2 = max entries
        // Returns: entry count, or (uint64_t)-1 on error
        case SYS_READDIR:
        {
            const char* upath = (const char*)arg0;
            FatDirInfo* uout = (FatDirInfo*)arg1;
            int max = (int)arg2;

            if (max <= 0) return (uint64_t)-1;
            if (!IsUserPtr(uout, sizeof(FatDirInfo) * (size_t)max))
                return (uint64_t)-1;

            char kpath[256];
            if (CopyUserString(upath, kpath, sizeof(kpath)) < 0)
                return (uint64_t)-1;

            if (!volumes[0]) return (uint64_t)-1;

            int n = FatReadDir(volumes[0], kpath, uout, max);
            return (uint64_t)(long)n;
        }

        // SYS_GETCWD (7)
        // arg0 = user buf, arg1 = size
        case SYS_GETCWD:
        {
            char* ubuf = (char*)arg0;
            size_t size = (size_t)arg1;

            if (size == 0) return (uint64_t)-1;
            if (!IsUserPtr(ubuf, size)) return (uint64_t)-1;

            const char* cwd = scheduler.current->process->cwd;
            size_t len = 0;
            while (cwd[len]) len++;
            len++; // include NUL

            if (len > size) return (uint64_t)-1;

            for (size_t i = 0; i < len; i++) ((volatile char*)ubuf)[i] = cwd[i];

            return (uint64_t)(len - 1); // return length excluding NUL
        }

        // SYS_CHDIR(8)
        // arg0 = path (user string)
        // Returns: 0 on success, (uint64_t)-1 on error
        case SYS_CHDIR:
        {
            const char* upath = (const char*)arg0;

            char kpath[256];
            if (CopyUserString(upath, kpath, sizeof(kpath)) < 0) return (uint64_t)-1;

            if (!volumes[0]) return (uint64_t)-1;

            // Resolve to an absolute path first
            char resolved[256];
            if (kpath[0] == '/')
            {
                // Already absolute - copy as-is
                size_t i = 0;
                while (kpath[i] && i < sizeof(resolved)-1)
                {
                    resolved[i] = kpath[i];
                    i++;
                }
                resolved[i] = '\0';
            }
            else
            {
                // Relative - join cwd + "/" + kpath
                const char* cwd = scheduler.current->process->cwd;
                size_t ci = 0;
                while (cwd[ci] && ci < sizeof(resolved)-1)
                {
                    resolved[ci] = cwd[ci];
                    ci++;
                }

                // Add separator only if cwd isn't already "/"
                if (ci > 1 && resolved[ci - 1] != '/')
                {
                    if (ci < sizeof(resolved)-1) resolved[ci++] = '/';
                }
                size_t ki = 0;
                while (kpath[ki] && ci < sizeof(resolved)-1)
                {
                    resolved[ci++] = kpath[ki++];
                }
                resolved[ci] = '\0';
            }

            char normalized[256];
            normalized[0] = '/';
            normalized[1] = '\0';
            size_t nlen = 1;

            const char* r = resolved;
            if (*r == '/') r++; // skip leading slash

            while (*r)
            {
                // Grab next segment
                char seg[256];
                size_t slen = 0;
                while (*r && *r != '/') seg[slen++] = *r++;
                seg[slen] = '\0';
                if (*r == '/') r++;

                if (slen == 0) continue; // double slash, skip
                if (slen == 1 && seg[0] == '.') continue; // "." - stay put

                if (slen == 2 && seg[0] == '.' && seg[1] == '.')
                {
                    // ".." - strip last component from normalized
                    // Walk back from end, stop at the leading '/'
                    if (nlen > 1)
                    {
                        nlen--; // Step off the trailing slahs (or end)
                        while (nlen > 1 && normalized[nlen - 1] != '/') nlen--;
                    }
                    // Already root
                }
                else
                {
                    // Normal segment - append to normalized
                    if (nlen > 1) normalized[nlen++] = '/'; // Separtor (skip if already root "/")
                    for (size_t i = 0; i < slen && nlen < 254; i++) normalized[nlen++] = seg[i];
                }
                normalized[nlen] = '\0';
            }

            // Verify the path actually exists and is a directory on the FAT volume
            FatDirInfo probe[1];
            int n = FatReadDir(volumes[0], normalized, probe, 1);
            if (n < 0) return (uint64_t)-1; // Path doesn't exist or not a directory

            // Commit
            size_t i = 0;
            while (normalized[i] && i < 255)
            {
                scheduler.current->process->cwd[i] = normalized[i];
                i++;
            }
            scheduler.current->process->cwd[i] = '\0';

            return 0;
        }

        default:
            return (uint64_t)-1; // ENOSYS
    }
}

extern "C" void syscall_entry();

void SyscallInit()
{
    // STAR: bits 47:32 = kernel CS (syscall), bits 63:48 = user CS (sysret)
    // uint64_t star = ((uint64_t)GDT_KERNEL_CODE << 32) |
    //                 ((uint64_t)(GDT_USER_CODE - 16) << 48);
    uint64_t star = ((uint64_t)GDT_KERNEL_CODE << 32) |
                    ((uint64_t)0x13 << 48);
    WriteMSR(0xC0000081, star);

    WriteMSR(0xC0000082, (uint64_t)syscall_entry);

    WriteMSR(0xC0000084, (1u << 9));

    uint64_t efer = ReadMSR(0xC0000080);
    WriteMSR(0xC0000080, efer | 0x1);
    // kprintf("[syscall] SyscallInit done. LSTAR=0x%llx\n", (uint64_t)syscall_entry);
}