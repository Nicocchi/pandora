bits 64

extern syscall_dispatch
extern tss

; Byte offset of rsp0 inside TSSEntry: reserved0(4 bytes) -> rsp0 at offset 4
TSS_RSP0_OFFSET equ 4

; GDT selectors — must match common.h
GDT_USER_CODE equ 0x23
GDT_USER_DATA equ 0x1B

section .bss
align 8
user_rsp_scratch: resq 1   ; temporary save for user RSP (single-core safe)

section .text
global syscall_entry

syscall_entry:
    ; -------------------------------------------------------------------------
    ; 1. Save user RSP into scratch — we are still on the user stack here
    ; -------------------------------------------------------------------------
    mov [user_rsp_scratch], rsp

    ; -------------------------------------------------------------------------
    ; 2. Switch to kernel stack via TSS.rsp0
    ;    MUST use a register — cannot do [rsp+offset] here because rsp now
    ;    points at user_rsp_scratch (from step 1), not the TSS.
    ;    lea into rsp gives us the TSS address in a register safely.
    ; -------------------------------------------------------------------------
    lea rsp, [rel tss]                ; rsp = address of tss symbol (temporary)
    mov rsp, [rsp + TSS_RSP0_OFFSET]  ; rsp = tss.rsp0  (kernel stack top)

    ; -------------------------------------------------------------------------
    ; 3. Now on the kernel stack — safe to re-enable interrupts
    ; -------------------------------------------------------------------------
    sti

    ; -------------------------------------------------------------------------
    ; 4. Build an iretq-style frame for clean user context restore on return
    ;    Stack layout (rsp = top = lowest address):
    ;      user RIP    (rcx — saved by syscall instruction)
    ;      user CS
    ;      user RFLAGS (r11 — saved by syscall instruction)
    ;      user RSP
    ;      user SS
    ; -------------------------------------------------------------------------
    push qword GDT_USER_DATA          ; user SS
    push qword [user_rsp_scratch]     ; user RSP
    push r11                          ; user RFLAGS
    push qword GDT_USER_CODE          ; user CS
    push rcx                          ; user RIP

    ; -------------------------------------------------------------------------
    ; 5. Save callee-saved registers (C ABI: rbx, rbp, r12-r15)
    ; -------------------------------------------------------------------------
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; -------------------------------------------------------------------------
    ; 5b. Preserve the caller-saved argument registers across the C dispatch.
    ;
    ;     The syscall ABI requires the kernel to preserve EVERY general-purpose
    ;     register except rax (return value), rcx and r11 (clobbered by the
    ;     syscall/sysret instructions themselves).  Userspace wrappers rely on
    ;     this: at -O2 the compiler hoists arg registers out of polling loops
    ;     (e.g. SYS_READ's `while (sys_read(...) == 0)`) and reloads only rax,
    ;     so rdi/rsi/rdx/r8/r9/r10 MUST survive the call.
    ;     (6 pushes = 48 bytes, keeps the 16-byte stack alignment parity.)
    ; -------------------------------------------------------------------------
    push rdi
    push rsi
    push rdx
    push r8
    push r9
    push r10

    ; -------------------------------------------------------------------------
    ; 6. Shuffle into C calling convention for syscall_dispatch(nr, a0, a1, a2)
    ;
    ;    Syscall ABI (from userspace):   rax=nr  rdi=arg0  rsi=arg1  rdx=arg2
    ;    C calling convention:           rdi=nr  rsi=arg0  rdx=arg1  rcx=arg2
    ;
    ;    Save arg2 (rdx) into rcx first — before rdx gets overwritten.
    ; -------------------------------------------------------------------------
    mov rcx, rdx        ; rcx = arg2
    mov rdx, rsi        ; rdx = arg1
    mov rsi, rdi        ; rsi = arg0
    mov rdi, rax        ; rdi = nr

    ; 5th C arg (r8) = pointer to the saved user register frame.
    ; rsp currently points at the lowest pushed register (r10), which is the
    ; base of the SyscallFrame struct used by fork()/exec().
    mov r8, rsp         ; r8 = SyscallFrame*

    call syscall_dispatch   ; return value in rax

    ; -------------------------------------------------------------------------
    ; 7. Restore the user's argument registers (rax keeps the return value),
    ;    then the callee-saved registers.
    ; -------------------------------------------------------------------------
    pop r10
    pop r9
    pop r8
    pop rdx
    pop rsi
    pop rdi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; -------------------------------------------------------------------------
    ; 8. Restore user context from the iretq-style frame (step 4)
    ;    Current stack (rsp -> RIP slot):
    ;      [rsp+ 0] user RIP   -> rcx   (sysretq reads RIP from rcx)
    ;      [rsp+ 8] user CS             (skip — STAR handles CS)
    ;      [rsp+16] user RFLAGS -> r11  (sysretq reads RFLAGS from r11)
    ;      [rsp+24] user RSP
    ;      [rsp+32] user SS             (skip — STAR handles SS)
    ; -------------------------------------------------------------------------
    pop rcx             ; user RIP   -> rcx
    add rsp, 8          ; skip user CS
    pop r11             ; user RFLAGS -> r11
    or  r11, 0x200      ; SYSCALL clears IF; re-enable before returning to ring 3
    pop rsp             ; restore user RSP  (SS slot implicitly discarded)

    ; -------------------------------------------------------------------------
    ; 9. Return to ring 3
    ;    IF must be clear before sysretq (hardware requirement)
    ;    sysretq: RIP<-rcx, RFLAGS<-r11, CS/SS from STAR, CPL->3
    ; -------------------------------------------------------------------------
    cli
    db 0x48, 0x0F, 0x07    ; sysretq — REX.W + 0F 07