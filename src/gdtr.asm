BITS 64

global gdt_flush

section .text

gdt_flush:
    ; RDI = pointer to GDTDescriptor
    lgdt [rdi]

    ; Reload segment registers
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Reload CS using far return
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ret
