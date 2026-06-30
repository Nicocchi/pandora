#include "syscall.h"

extern int main(int argc, char** argv);

// argc/argv are placed on the stack by the kernel's exec():
//   [rsp]   = argc
//   [rsp+8] = argv[0], [rsp+16] = argv[1], ... , NULL
// Naked so the compiler doesn't disturb rsp before we read them.
extern "C" __attribute__((naked)) void _start()
{
    asm volatile(
        "mov (%rsp), %rdi\n"    // argc
        "lea 8(%rsp), %rsi\n"   // argv
        "call main\n"
        "mov %eax, %edi\n"      // exit code = main()'s return
        "xor %eax, %eax\n"      // SYS_EXIT = 0
        "syscall\n"
    );
}
