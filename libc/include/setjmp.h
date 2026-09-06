/*
 * SzpontOS - POSIX setjmp.h
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SETJMP_H
#define _SETJMP_H

/* x86_64 jmp_buf stores: rbx, rbp, r12, r13, r14, r15, rsp, rip */
typedef unsigned long jmp_buf[8];
typedef jmp_buf sigjmp_buf;

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

int _setjmp(jmp_buf env);
void _longjmp(jmp_buf env, int val) __attribute__((noreturn));

int sigsetjmp(sigjmp_buf env, int savesigs);
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */
