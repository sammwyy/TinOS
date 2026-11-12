/* Interrupt descriptor table and the C side of the interrupt dispatcher. */

#include <tinos/arch.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/process.h>
#include <tinos/string.h>
#include <tinos/syscall.h>
#include <tinos/thread.h>

#define IDT_ENTRIES 256

typedef struct PACKED {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} idt_entry_t;

typedef struct PACKED {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t   idt[IDT_ENTRIES];
static idt_ptr_t     idt_ptr;
static irq_handler_t irq_handlers[16];

/* Stubs from isr.S */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);
extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);
extern void isr128(void);

static void *const exception_stubs[32] = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static void *const irq_stubs[16] = {
    irq0, irq1, irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15,
};

static const char *const exception_names[32] = {
    "Divide by zero",          "Debug",
    "Non-maskable interrupt",  "Breakpoint",
    "Overflow",                "Bound range exceeded",
    "Invalid opcode",          "Device not available",
    "Double fault",            "Coprocessor segment overrun",
    "Invalid TSS",             "Segment not present",
    "Stack-segment fault",     "General protection fault",
    "Page fault",              "Reserved",
    "x87 floating-point",      "Alignment check",
    "Machine check",           "SIMD floating-point",
    "Virtualisation",          "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection",    "VMM communication",
    "Security exception",      "Reserved",
};

static void idt_set(int vector, void *handler, uint8_t type_attr)
{
    uint32_t addr           = (uint32_t)(uintptr_t)handler;
    idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_high = (uint16_t)((addr >> 16) & 0xFFFF);
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));

    pic_remap(0x20, 0x28);

    for (int i = 0; i < 32; i++)
        idt_set(i, exception_stubs[i], 0x8E); /* present, ring0, 32-bit gate */
    for (int i = 0; i < 16; i++)
        idt_set(32 + i, irq_stubs[i], 0x8E);
    idt_set(128, isr128, 0xEE); /* interrupt gate accessible from ring 3 */

    idt_ptr.limit = (uint16_t)(sizeof(idt) - 1);
    idt_ptr.base  = (uint32_t)(uintptr_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

/* The IDT and its vector table are identical for every core; an SMP
 * application processor just points its own IDTR at this same table. */
void idt_load_shared(void)
{
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
}

void irq_install(int irq, irq_handler_t handler)
{
    if (irq < 0 || irq >= 16)
        return;
    bool saved = irq_save();
    irq_handlers[irq] = handler;
    pic_unmask_irq(irq);
    irq_restore(saved);
}

void irq_uninstall(int irq)
{
    if (irq < 0 || irq >= 16)
        return;
    bool saved = irq_save();
    irq_handlers[irq] = NULL;
    pic_mask_irq(irq);
    irq_restore(saved);
}

/* Called from isr_common with a pointer to the saved register frame. */
regs_t *isr_dispatch(regs_t *regs)
{
    if (regs->int_no < 32) {
        if ((regs->cs & 3) == 3 && process_active())
            process_abort((int)regs->int_no);
        const char *name = exception_names[regs->int_no];
        uint32_t    cr2  = 0;
        if (regs->int_no == 14)
            __asm__ volatile("movl %%cr2, %0" : "=r"(cr2));

        /* Exceptions are always fatal in TinOS: report them loudly with
         * enough state to identify the faulting instruction. */
        panic("CPU exception %u (%s)\nerror=0x%08x eip=0x%08x cs=0x%04x "
              "eflags=0x%08x cr2=0x%08x\neax=0x%08x ebx=0x%08x ecx=0x%08x "
              "edx=0x%08x\nesi=0x%08x edi=0x%08x ebp=0x%08x esp=0x%08x",
              regs->int_no, name, regs->err_code, regs->eip, regs->cs,
              regs->eflags, cr2, regs->eax, regs->ebx, regs->ecx, regs->edx,
              regs->esi, regs->edi, regs->ebp, (uint32_t)(uintptr_t)regs);
    }
    if (regs->int_no == 128) {
        syscall_dispatch(regs);
        process_maybe_preempt(regs);
        return regs;
    }

    int irq = (int)regs->int_no - 32;
    if (irq >= 0 && irq < 16) {
        /* Spurious IRQ7/IRQ15 must not be acknowledged. */
        if (irq == 7 || irq == 15) {
            outb(irq == 7 ? 0x20 : 0xA0, 0x0B);
            uint8_t isr_reg = inb(irq == 7 ? 0x20 : 0xA0);
            if (!(isr_reg & (1 << (irq & 7)))) {
                if (irq == 15)
                    pic_send_eoi(2); /* still EOI the master for the slave */
                return regs;
            }
        }
        if (irq_handlers[irq])
            irq_handlers[irq](regs);
        pic_send_eoi(irq);
        if (irq == 0) {
            process_on_tick(regs);
            return thread_preempt(regs);
        }
    }
    return regs;
}
