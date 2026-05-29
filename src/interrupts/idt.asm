; ------------------------------------------------------------
; idt.asm
;
; x86_64 Interrupt Descriptor Table (IDT) and Interrupt Service Routine (ISR)
; implementation for Pandora OS.
;
; This file contains:
;   - IDT loading routine
;   - CPU exception stubs
;   - Hardware IRQ stubs
;   - Common interrupt dispatch entry
;   - Register save/restore macros
;
; All interrupts eventually funnel into `isr_common`, which preserves the
; CPU register state and forwards execution to the C++ interrupt dispatcher.
; ------------------------------------------------------------

BITS 64

section .text

global idt_flush

extern interrupt_dispatch

;------------------------------------------------------------------------------
; void idt_flush(IDTPtr* idtr)
;
; Loads the Interrupt Descriptor Table Register (IDTR) using the supplied
; pointer structure.
;
; Parameters:
;   rdi - Pointer to IDTPtr structure
;------------------------------------------------------------------------------
idt_flush:
    lidt [rdi]
    ret

;------------------------------------------------------------------------------
; ISR_NOERRCODE
;
; Generates an ISR stub for exceptions that do NOT automatically push
; a hardware error code.
;
; Stack layout:
;   interrupt_number
;   error_code (fake 0)
;------------------------------------------------------------------------------
%macro ISR_NOERRCODE 1
    global isr%1
    isr%1:
        push qword 0          ; Fake error code
        push qword %1         ; Interrupt number
        jmp isr_common
%endmacro

;------------------------------------------------------------------------------
; ISR_ERRCODE
;
; Generates an ISR stub for exceptions that automatically push a hardware
; error code onto the stack.
;
; Stack layout:
;   interrupt_number
;   hardware_error_code
;------------------------------------------------------------------------------
%macro ISR_ERRCODE 1
    global isr%1
    isr%1:
        push qword %1         ; Interrupt number
        jmp isr_common
%endmacro

;------------------------------------------------------------------------------
; IRQ
;
; Generates a hardware IRQ stub.
;
; Hardware IRQs do not provide an error code, so a fake error code of 0
; is pushed for consistency with the InterruptFrame structure.
;------------------------------------------------------------------------------
%macro IRQ 2
    global irq%1
    irq%1:
        mov byte [0xB8000], '0'
        push qword 0          ; Fake error code
        push qword %2         ; IRQ vector number
        jmp isr_common
%endmacro

;------------------------------------------------------------------------------
; CPU Exceptions
;------------------------------------------------------------------------------
ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7

ISR_ERRCODE 8

ISR_NOERRCODE 9

ISR_ERRCODE 10
ISR_ERRCODE 11
ISR_ERRCODE 12
ISR_ERRCODE 13
ISR_ERRCODE 14

ISR_NOERRCODE 15
ISR_NOERRCODE 16

ISR_ERRCODE 17

ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20

ISR_ERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28

ISR_ERRCODE 29
ISR_ERRCODE 30

ISR_NOERRCODE 31

ISR_NOERRCODE 128
ISR_NOERRCODE 177

;------------------------------------------------------------------------------
; PIC Hardware IRQs
;------------------------------------------------------------------------------
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

;------------------------------------------------------------------------------
; isr_common
;
; Common interrupt handler entry point.
;
; Responsibilities:
;   - Save CPU register state
;   - Pass InterruptFrame* to C++
;   - Restore register state
;   - Return from interrupt using iretq
;------------------------------------------------------------------------------
isr_common:

    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    mov rdi, rsp
    call interrupt_dispatch

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15

    ; add rsp, 16
    iretq