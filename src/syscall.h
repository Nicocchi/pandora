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

// Snapshot of the user register state saved by syscall_entry.asm on the kernel
// stack. The field order MUST match the push sequence in syscall_entry (see
// that file for the layout). syscall_dispatch receives a pointer to this so
// fork() can clone the caller's context and exec() can rewrite it in place.
struct SyscallFrame
{
    uint64_t r10, r9, r8, rdx, rsi, rdi;     // +0   .. +40
    uint64_t r15, r14, r13, r12, rbx, rbp;   // +48  .. +88
    uint64_t rip, cs, rflags, rsp, ss;       // +96  .. +128 (rip<-rcx, rflags<-r11)
};

void SyscallInit();
