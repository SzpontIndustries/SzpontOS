#include <arch/x86_64/gdt.h>
#include <arch/x86_64/io.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

struct __attribute__((packed)) gdt_descriptor {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
};

struct __attribute__((packed)) gdt_tss_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
    uint32_t base_upper;
    uint32_t reserved;
};

struct __attribute__((packed)) gdt_table {
    struct gdt_entry null_entry;    /* 0x00 */
    struct gdt_entry kernel_code;   /* 0x08 */
    struct gdt_entry kernel_data;   /* 0x10 */
    struct gdt_entry user_data;     /* 0x18 */
    struct gdt_entry user_code;     /* 0x20 */
    struct gdt_tss_entry tss_entry; /* 0x28 (16 bytes) */
};

static struct gdt_table g_gdt;
static struct gdt_descriptor g_gdt_desc;
static tss_entry_t g_tss;

extern void gdt_load(struct gdt_descriptor *desc);

void gdt_init(void) {
    memset(&g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    /* Kernel Code 64-bit: Ring 0, Exec/Read, Conforming=0, 64-bit=1 */
    g_gdt.kernel_code.limit_low = 0xFFFF;
    g_gdt.kernel_code.access = 0x9A;      /* Present, Ring 0, Executable, Readable */
    g_gdt.kernel_code.granularity = 0x20; /* 64-bit long mode flag (L=1, D=0) */

    /* Kernel Data 64-bit: Ring 0, Read/Write */
    g_gdt.kernel_data.limit_low = 0xFFFF;
    g_gdt.kernel_data.access = 0x92; /* Present, Ring 0, Writable */
    g_gdt.kernel_data.granularity = 0x00;

    /* User Data 64-bit: Ring 3, Read/Write */
    g_gdt.user_data.limit_low = 0xFFFF;
    g_gdt.user_data.access = 0xF2; /* Present, Ring 3, Writable */
    g_gdt.user_data.granularity = 0x00;

    /* User Code 64-bit: Ring 3, Exec/Read */
    g_gdt.user_code.limit_low = 0xFFFF;
    g_gdt.user_code.access = 0xFA;      /* Present, Ring 3, Executable, Readable */
    g_gdt.user_code.granularity = 0x20; /* 64-bit flag */

    /* TSS Setup */
    uintptr_t tss_base = (uintptr_t)&g_tss;
    uint32_t tss_limit = sizeof(g_tss) - 1;

    g_gdt.tss_entry.limit_low = (uint16_t)(tss_limit & 0xFFFF);
    g_gdt.tss_entry.base_low = (uint16_t)(tss_base & 0xFFFF);
    g_gdt.tss_entry.base_mid = (uint8_t)((tss_base >> 16) & 0xFF);
    g_gdt.tss_entry.access = 0x89; /* Present, Ring 0, Available 64-bit TSS */
    g_gdt.tss_entry.granularity = (uint8_t)((tss_limit >> 16) & 0x0F);
    g_gdt.tss_entry.base_high = (uint8_t)((tss_base >> 24) & 0xFF);
    g_gdt.tss_entry.base_upper = (uint32_t)(tss_base >> 32);
    g_gdt.tss_entry.reserved = 0;

    g_tss.iomap_base = sizeof(g_tss);

    g_gdt_desc.limit = sizeof(g_gdt) - 1;
    g_gdt_desc.base = (uint64_t)&g_gdt;

    __asm__ volatile("lgdt %0\n\t"
                     "mov $0x10, %%ax\n\t"
                     "mov %%ax, %%ds\n\t"
                     "mov %%ax, %%es\n\t"
                     "mov %%ax, %%fs\n\t"
                     "mov %%ax, %%gs\n\t"
                     "mov %%ax, %%ss\n\t"
                     "pushq $0x08\n\t"
                     "leaq 1f(%%rip), %%rax\n\t"
                     "pushq %%rax\n\t"
                     "lretq\n\t"
                     "1:\n\t"
                     "mov $0x28, %%ax\n\t"
                     "ltr %%ax\n\t"
                     :
                     : "m"(g_gdt_desc)
                     : "rax", "memory");

    klog_info("GDT & TSS initialized successfully (CS=0x08, DS=0x10, TSS=0x28)");
}

uintptr_t g_current_kernel_stack = 0;

void gdt_set_kernel_stack(uintptr_t stack) {
    g_tss.rsp0 = stack;
    g_current_kernel_stack = stack;
}

uint8_t g_default_fpu_state[512] __attribute__((aligned(16)));

void fpu_init(void) {
    uint64_t cr0 = read_cr0();
    cr0 &= ~(1ULL << 2); /* Clear EM */
    cr0 |= (1ULL << 1);  /* Set MP */
    cr0 |= (1ULL << 5);  /* Set NE */
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= (1ULL << 9);  /* Set OSFXSR (SSE enable) */
    cr4 |= (1ULL << 10); /* Set OSXMMEXCPT */
    write_cr4(cr4);

    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    __asm__ volatile("fxsave64 %0" : "=m"(g_default_fpu_state));

    klog_info("FPU & SSE / AVX SIMD instructions enabled (CR0 & CR4 configured)");
}
