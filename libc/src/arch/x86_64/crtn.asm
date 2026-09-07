; crtn.asm - System V ABI termination hook for SzpontOS
; (C) Copyright by Szpont Industries. All rights reserved.

[bits 64]
default rel

section .init
    leave
    ret

section .fini
    leave
    ret
