; crti.asm - System V ABI initialization hook for SzpontOS
; (C) Copyright by Szpont Industries. All rights reserved.

[bits 64]
default rel

section .init
global _init:function
_init:
    push rbp
    mov rbp, rsp

section .fini
global _fini:function
_fini:
    push rbp
    mov rbp, rsp
