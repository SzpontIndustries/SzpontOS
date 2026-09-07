[bits 64]
default rel

global _start
global __dso_handle:weak
extern main
extern exit
extern environ
extern __libc_init_array
extern __init_array_start
extern __init_array_end
extern __preinit_array_start
extern __preinit_array_end
extern _init:weak
extern _fini:weak
extern atexit

section .data
__dso_handle: dq __dso_handle

section .text

_start:
    ; Terminate stack frame for debuggers
    xor rbp, rbp

    ; Extract argc, argv, envp from stack
    ; [rsp] = argc
    ; [rsp + 8] = argv
    mov rdi, [rsp]        ; 1st arg: argc
    lea rsi, [rsp + 8]    ; 2nd arg: argv

    ; envp is after argv array (argc + 1 pointers)
    mov rax, rdi
    inc rax
    shl rax, 3
    lea rdx, [rsi + rax]  ; 3rd arg: envp

    ; Set environ = envp
    mov [environ], rdx

    ; Align stack to 16 bytes before function calls (System V ABI requirement)
    and rsp, -16

    ; Preserve argc, argv, envp across initialization
    push rdx
    push rsi
    push rdi
    sub rsp, 8            ; Align stack to 16 bytes for call

    ; Call _init if present (runs crtbegin frame_dummy)
    mov rax, [_init wrt ..gotpc]
    test rax, rax
    jz .no_init
    call rax
.no_init:

    ; Register _fini with atexit if present
    mov rdi, [_fini wrt ..gotpc]
    test rdi, rdi
    jz .no_fini
    call atexit wrt ..plt
.no_fini:

    ; Execute preinit and init arrays (C++ global constructors)
    lea rdi, [__init_array_start]
    lea rsi, [__init_array_end]
    lea rdx, [__preinit_array_start]
    lea rcx, [__preinit_array_end]
    call __libc_init_array wrt ..plt

    add rsp, 8
    pop rdi
    pop rsi
    pop rdx

    call main wrt ..plt

    ; Exit with main's return code
    mov rdi, rax
    call exit wrt ..plt

.hang:
    hlt
    jmp .hang
