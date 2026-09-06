;
; SzpontOS - setjmp and longjmp for x86_64 ABI
; (C) Copyright by Szpont Industries. All rights reserved.
;

[BITS 64]
global setjmp
global longjmp
global _setjmp
global _longjmp
global sigsetjmp
global siglongjmp

section .text

; int setjmp(jmp_buf env)
; RDI = env (pointer to 8 x 64-bit uint64_t array)
setjmp:
_setjmp:
sigsetjmp:
    mov [rdi + 0],  rbx
    mov [rdi + 8],  rbp
    mov [rdi + 16], r12
    mov [rdi + 24], r13
    mov [rdi + 32], r14
    mov [rdi + 40], r15
    lea rdx, [rsp + 8]   ; RSP above return address
    mov [rdi + 48], rdx
    mov rdx, [rsp]       ; Return RIP
    mov [rdi + 56], rdx
    xor eax, eax         ; Return 0
    ret

; void longjmp(jmp_buf env, int val)
; RDI = env, RSI = val
longjmp:
_longjmp:
siglongjmp:
    mov eax, esi
    test eax, eax
    jnz .val_ok
    mov eax, 1           ; If val == 0, return 1
.val_ok:
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rsp, [rdi + 48]
    mov rdx, [rdi + 56]
    jmp rdx              ; Jump directly to saved RIP
