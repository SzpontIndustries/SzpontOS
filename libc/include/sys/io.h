/*
 * SzpontOS - POSIX sys/io.h (x86_64 IO port primitives)
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#ifndef _SYS_IO_H
#define _SYS_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint8_t val, uint16_t port) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outw(uint16_t val, uint16_t port) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void outl(uint32_t val, uint16_t port) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline int iopl(int level) {
    (void)level;
    return 0;
}

static inline int ioperm(unsigned long from, unsigned long num, int turn_on) {
    (void)from;
    (void)num;
    (void)turn_on;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IO_H */
