; ----------------------------------------------------------------------------
;  context_switch.asm
;
;  void context_switch(TaskContext *old_ctx, TaskContext *new_ctx)
;
;  rdi = old_ctx   (TaskContext* of the task being suspended)
;  rsi = new_ctx   (TaskContext* of the task being resumed)
;
;  Saves all callee-saved registers + RSP into old_ctx.
;  Restores them from new_ctx.
;  Returns into the new task (its saved RIP is the return address on stack).
;
;  TaskContext layout (must match struct TaskContext in scheduler.h):
;    offset  0: rbx
;    offset  8: rbp
;    offset 16: r12
;    offset 24: r13
;    offset 32: r14
;    offset 40: r15
;    offset 48: rsp
; ----------------------------------------------------------------------------

BITS 64

global context_switch
extern tss ; TSSentry defined in gdt.cpp
; global tss_set_rsp0

extern tss_rsp0_ptr     ; Pointer to tss.rsp0, defined in gdt.cpp

section .text

; ----------------------------------------------------------------------------
;  context_switch(TaskContext *old_ctx, TaskContext *new_ctx)
; ----------------------------------------------------------------------------
context_switch:
    ; Save current task's callee-saved registers into old_ctx (rdi)
    mov [rdi + 0], rbx
    mov [rdi + 8], rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    mov [rdi + 48], rsp ; Save current kernel stack pointer

    ; Restore next task's callee-saved registers from new_ctx (rsi)
    mov rbx, [rsi + 0]
    mov rbp, [rsi + 8]
    mov r12, [rsi + 16]
    mov r13, [rsi + 24]
    mov r14, [rsi + 32]
    mov r15, [rsi + 40]
    mov rsp, [rsi + 48] ; Switch to new task's kernel stack

    ; The new task's RIP is the return address sitting on its stack.
    ; `ret` pops it and jumps there - either back into a previously
    ; preempted task, or into task_entry_stub for a brand-new task.
    ret

; ----------------------------------------------------------------------------
;  tss_set_rsp0(uint64_t rsp0)
;
;  rdi = new rsp0 value
;
;  Updates tss.rsp0 so the CPU knows which kernel stack to use on the
;  next Ring 3 → Ring 0 transition.  Must be called on every context
;  switch to a new task.
; ----------------------------------------------------------------------------
tss_set_rsp0:
    ; mov rax, [rel tss_rsp0_ptr] ; RAX = &tss.rsp0
    mov [rel tss + 4], rdi
    ; mov [rax], rdi              ; tss.rsp0 = new value
    ret

global user_entry_iretq
user_entry_iretq:
    ; Stack already has iretq frame: RIP, CS, RFLAGS, RSP, SS
    ; Zero general purpose registers before jumping to user code
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rdi, rdi
    xor rsi, rsi
    xor r8,  r8
    xor r9,  r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rbp, rbp
    iretq