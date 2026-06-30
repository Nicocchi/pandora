#pragma once

#include <stdint.h>

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_FORK 3
#define SYS_EXEC 4
#define SYS_WAIT 5
#define SYS_READDIR 6
#define SYS_GETCWD 7
#define SYS_CHDIR 8

// Must match struct FatDirInfo in the kernel (src/fs/fat32.h).
struct DirEntry
{
    char name[64];
    uint32_t size;
    uint8_t is_dir;
};

static inline void sys_exit(int code)
{
    asm volatile(
        "syscall"
        :
        : "a"(SYS_EXIT), "D"((uint64_t)code)
        : "rcx", "r11", "memory"
    );
    __builtin_unreachable();
}

static inline long sys_write(int fd, const char* buf, uint64_t len)
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WRITE), "D"((uint64_t)fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long sys_read(int fd, char* buf, uint64_t len)
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READ), "D"((uint64_t)fd), "S"(buf), "d"(len)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Fill `out` (capacity `max` entries) with the contents of directory `path`.
// Returns the number of entries, or -1 on error.
static inline long sys_readdir(const char* path, struct DirEntry* out, uint64_t max)
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_READDIR), "D"(path), "S"(out), "d"(max)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline char sys_getchar()
{
    char c = 0;
    sys_read(0, &c, 1);
    return c;
}

static inline long sys_fork()
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_FORK)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Replace the current process image with the program at `path`, passing
// `argv` (NULL-terminated). Only returns (-1) if exec fails.
static inline long sys_exec(const char* path, char* const argv[])
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_EXEC), "D"(path), "S"(argv)
        : "rcx", "r11", "memory"
    );
    return ret;
}

// Block until this process's child exits; returns the child's exit code.
static inline long sys_wait()
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_WAIT)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long sys_getcwd(char* buf, uint64_t size)
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_GETCWD), "D"(buf), "S"(size)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long sys_chdir(const char* path)
{
    long ret;
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(SYS_CHDIR), "D"(path)
        : "rcx", "r11", "memory"
    );
    return ret;
}