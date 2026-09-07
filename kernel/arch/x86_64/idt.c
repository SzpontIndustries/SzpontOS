#include <arch/x86_64/idt.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/io.h>
#include <drivers/ioapic.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/panic.h>
#include <sched/process.h>
#include <sched/sched.h>

struct __attribute__((packed)) idt_entry {
    uint16_t isr_low;   /* The lower 16 bits of the ISR's address */
    uint16_t kernel_cs; /* The GDT segment selector that the CPU will load into CS */
    uint8_t ist;        /* IST index in TSS */
    uint8_t attributes; /* Type and attributes (e.g. 0x8E for 64-bit Interrupt Gate) */
    uint16_t isr_mid;   /* The higher 16 bits of the lower 32 bits of the ISR's address */
    uint32_t isr_high;  /* The higher 32 bits of the ISR's address */
    uint32_t reserved;  /* Set to zero */
};

struct __attribute__((packed)) idtr {
    uint16_t limit;
    uint64_t base;
};

static struct idt_entry g_idt[IDT_ENTRIES] __attribute__((aligned(0x10)));
static struct idtr g_idtr;
static isr_handler_t g_interrupt_handlers[IDT_ENTRIES];

extern void *isr_stub_table[];
extern void idt_load(struct idtr *ptr);

static const char *g_exception_names[32] = {"Divide-by-zero (#DE)",
                                            "Debug (#DB)",
                                            "Non-maskable Interrupt (#NMI)",
                                            "Breakpoint (#BP)",
                                            "Overflow (#OF)",
                                            "Bound Range Exceeded (#BR)",
                                            "Invalid Opcode (#UD)",
                                            "Device Not Available (#NM)",
                                            "Double Fault (#DF)",
                                            "Coprocessor Segment Overrun",
                                            "Invalid TSS (#TS)",
                                            "Segment Not Present (#NP)",
                                            "Stack-Segment Fault (#SS)",
                                            "General Protection Fault (#GP)",
                                            "Page Fault (#PF)",
                                            "Reserved",
                                            "x87 Floating-Point Exception (#MF)",
                                            "Alignment Check (#AC)",
                                            "Machine Check (#MC)",
                                            "SIMD Floating-Point Exception (#XM)",
                                            "Virtualization Exception (#VE)",
                                            "Control Protection Exception (#CP)",
                                            "Reserved",
                                            "Reserved",
                                            "Reserved",
                                            "Reserved",
                                            "Reserved",
                                            "Reserved",
                                            "Hypervisor Injection Exception",
                                            "VMM Communication Exception",
                                            "Security Exception",
                                            "Reserved"};

void idt_set_gate(uint8_t vector, void *handler, uint8_t flags) {
    uintptr_t addr = (uintptr_t)handler;
    g_idt[vector].isr_low = (uint16_t)(addr & 0xFFFF);
    g_idt[vector].kernel_cs = GDT_KERNEL_CODE_SEL;
    g_idt[vector].ist = 0;
    g_idt[vector].attributes = flags;
    g_idt[vector].isr_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    g_idt[vector].isr_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    g_idt[vector].reserved = 0;
}

void isr_register_handler(uint8_t vector, isr_handler_t handler) {
    g_interrupt_handlers[vector] = handler;
}

void isr_handler(interrupt_frame_t *frame) {
    if (frame->int_no < 32) {
        /* CPU Exception */
        uint64_t cr2 = (frame->int_no == 14) ? read_cr2() : 0;

        if ((frame->cs & 3) == 3) {
            /* User-mode (Ring 3) exception: terminate faulting process instead of kernel panic */
            process_t *curr_proc = sched_get_current_process();

            klog_error("================ USER CPU EXCEPTION ================");
            klog_error("Exception [%u]: %s in process '%s' (PID %d)", (uint32_t)frame->int_no,
                       g_exception_names[frame->int_no], curr_proc ? curr_proc->name : "unknown",
                       curr_proc ? curr_proc->pid : -1);
            klog_error("Error Code: 0x%lx, CR2 (Fault Addr): 0x%lx", frame->err_code, cr2);
            klog_error("RIP: 0x%016lx  CS: 0x%04lx  RFLAGS: 0x%016lx", frame->rip, frame->cs, frame->rflags);
            klog_error("RSP: 0x%016lx  SS: 0x%04lx  RBP:    0x%016lx", frame->rsp, frame->ss, frame->rbp);
            klog_error("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx", frame->rax, frame->rbx, frame->rcx);
            klog_error("RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx", frame->rdx, frame->rsi, frame->rdi);
            klog_error("R8:  0x%016lx  R9:  0x%016lx  R10: 0x%016lx", frame->r8, frame->r9, frame->r10);
            klog_error("R11: 0x%016lx  R12: 0x%016lx  R13: 0x%016lx", frame->r11, frame->r12, frame->r13);
            klog_error("R14: 0x%016lx  R15: 0x%016lx", frame->r14, frame->r15);

            if (frame->rsp >= 0x1000 && frame->rsp < 0x800000000000ULL) {
                uint64_t *sp = (uint64_t *)frame->rsp;
                klog_error("Stack top [RSP]: 0x%016lx  [RSP+8]: 0x%016lx", sp[0], sp[1]);
                klog_error("Stack [RSP+16]:  0x%016lx  [RSP+24]: 0x%016lx", sp[2], sp[3]);
            }
            klog_error("====================================================");

            process_exit(128 + frame->int_no);
            while (1) {
                sched_yield();
            }
        }

        /* Kernel-mode exception: panic */
        klog_error("================ KERNEL CPU EXCEPTION ================");
        klog_error("Exception [%u]: %s", (uint32_t)frame->int_no, g_exception_names[frame->int_no]);
        klog_error("Error Code: 0x%lx, CR2 (Fault Addr): 0x%lx", frame->err_code, cr2);
        klog_error("RIP: 0x%016lx  CS: 0x%04lx  RFLAGS: 0x%016lx", frame->rip, frame->cs, frame->rflags);
        klog_error("RSP: 0x%016lx  SS: 0x%04lx  RBP:    0x%016lx", frame->rsp, frame->ss, frame->rbp);
        klog_error("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx", frame->rax, frame->rbx, frame->rcx);
        klog_error("RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx", frame->rdx, frame->rsi, frame->rdi);
        klog_error("R8:  0x%016lx  R9:  0x%016lx  R10: 0x%016lx", frame->r8, frame->r9, frame->r10);
        klog_error("R11: 0x%016lx  R12: 0x%016lx  R13: 0x%016lx", frame->r11, frame->r12, frame->r13);
        klog_error("R14: 0x%016lx  R15: 0x%016lx", frame->r14, frame->r15);
        klog_error("======================================================");

        panic_with_frame(frame, "Unhandled Kernel CPU Exception #%u: %s", (uint32_t)frame->int_no,
                         g_exception_names[frame->int_no]);
    }

    /* For timer interrupts (IRQ0 / vector 32), send EOI BEFORE handler execution
     * because the timer handler invokes the scheduler (sched_tick -> sched_yield).
     * If EOI were deferred until after context switch, the PIC / LAPIC In-Service
     * Register would stay latched, blocking all equal or lower priority interrupts
     * (including IRQ1 for the keyboard) while other threads execute!
     *
     * Device handlers (e.g. IRQ1 keyboard) execute FIRST so they consume hardware
     * port data before EOI allows subsequent hardware interrupts. */
    bool eoi_sent = false;
    if (frame->int_no == 32) {
        outb(0x20, 0x20); /* Master PIC EOI */
        if (ioapic_is_active()) {
            lapic_eoi();
        }
        eoi_sent = true;
    }

    /* Execute registered handler */
    if (g_interrupt_handlers[frame->int_no]) {
        g_interrupt_handlers[frame->int_no](frame);
    }

    /* Send End of Interrupt for non-timer interrupts after hardware data is consumed */
    if (!eoi_sent) {
        if (frame->int_no >= 32 && frame->int_no <= 47) {
            outb(0x20, 0x20); /* Master PIC EOI */
            if (frame->int_no >= 40) {
                outb(0xA0, 0x20); /* Slave PIC EOI */
            }
        }
        if (ioapic_is_active()) {
            lapic_eoi();
        }
    }
}

void idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    memset(g_interrupt_handlers, 0, sizeof(g_interrupt_handlers));

    /* Initialize all 256 gates with ISR stubs */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        /* 0x8E = Present, Ring 0, 64-bit Interrupt Gate */
        idt_set_gate(i, isr_stub_table[i], 0x8E);
    }

    g_idtr.limit = sizeof(g_idt) - 1;
    g_idtr.base = (uint64_t)g_idt;

    idt_load(&g_idtr);

    klog_info("IDT initialized with 256 interrupt vectors");
}
