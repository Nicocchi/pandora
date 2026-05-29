; ------------------------------------------------------------
; gdt.asm
; x86_64 GDT and TSS descriptor loading routines.
;
; Provides low-level assembly interfaces required to install
; the Global Descriptor Table and Task State Segment into the
; processor.
;
; This module performs:
;   - GDTR loading via lgdt
;   - Segment register reloading
;   - Code segment synchronization via far return
;   - Task Register loading via ltr
;
; These routines finalize CPU descriptor table initialization
; after the kernel constructs the GDT and TSS structures in C++.
; ------------------------------------------------------------

BITS 64

global gdt_flush
global tss_flush

section .text

; ------------------------------------------------------------
; Loads the Global Descriptor Table and reloads all segment
; registers using the newly installed descriptors.
;
; Input:
;   RDI = pointer to GDTDescriptor structure
;
; This routine performs:
;   - lgdt
;   - data segment reload
;   - far return to reload CS
; ------------------------------------------------------------
gdt_flush:
    ; Load GDTR register | rdi = pointer to GDTDescriptor
    lgdt [rdi]

    ; Reload data segment selectors using the
    ; kernel data descriptor (0x10)
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; CS cannot be directly modified.
    ; Perform a far return to reload CS using
    ; the kernel code selector (0x08)
    push 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    ret

; ------------------------------------------------------------
; Loads the Task State Segment into the CPU Task Register.
;
; Uses selector:
;   0x28 = GDT entry 5
;
; The TSS provides:
;   - Ring transition kernel stacks
;   - Interrupt Stack Table support
; ------------------------------------------------------------
tss_flush:
    mov ax, 0x28
    ltr ax
    ret
