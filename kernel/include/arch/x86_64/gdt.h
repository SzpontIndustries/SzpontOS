#ifndef SZPONTOS_ARCH_X86_64_GDT_H
#define SZPONTOS_ARCH_X86_64_GDT_H

#include <kernel/types.h>

#define GDT_KERNEL_CODE_SEL 0x08
#define GDT_KERNEL_DATA_SEL 0x10
#define GDT_USER_DATA_SEL (0x18 | 3)
#define GDT_USER_CODE_SEL (0x20 | 3)
#define GDT_TSS_SEL 0x28

struct __attribute__((packed)) tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

typedef struct tss_entry tss_entry_t;

void gdt_init(void);
void gdt_set_kernel_stack(uintptr_t stack);
void fpu_init(void);
extern uint8_t g_default_fpu_state[512] __attribute__((aligned(16)));

#endif /* SZPONTOS_ARCH_X86_64_GDT_H */
