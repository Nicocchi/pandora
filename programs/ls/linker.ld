OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        build/obj/../libc/start.cpp.o(.text*)
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*)   }
    .bss    : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }
}
