/* x86 architecture services: descriptor tables, interrupts, timer, RTC. */
#ifndef TINOS_ARCH_H
#define TINOS_ARCH_H

#include <tinos/kernel.h>
#include <tinos/types.h>

/* Register frame pushed by the interrupt stubs (see arch/x86/isr.S). */
typedef struct {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss; /* pushed by the CPU */
} regs_t;

/* Global descriptor table */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10

void gdt_init(void);
const void *gdt_pointer(void); /* the 6-byte GDTR image, for AP bring-up */

/* Interrupt descriptor table */
typedef void (*irq_handler_t)(regs_t *regs);

void idt_init(void);
void idt_load_shared(void); /* lidt the existing table - for an AP core */
void irq_install(int irq, irq_handler_t handler);
void irq_uninstall(int irq);

/* Programmable interrupt controller */
void pic_remap(int master_offset, int slave_offset);
void pic_mask_irq(int irq);
void pic_unmask_irq(int irq);
void pic_send_eoi(int irq);

/* Timer */
#define TIMER_HZ 1000u

void     timer_init(uint32_t frequency);
uint64_t timer_ticks(void);       /* milliseconds since timer_init()      */
uint32_t timer_seconds(void);     /* whole seconds since timer_init()     */
void     timer_sleep(uint32_t ms);/* busy/HLT wait, requires interrupts   */

/* Real time clock */
typedef struct {
    uint16_t year;
    uint8_t  month, day, hour, minute, second;
    bool     valid;
} rtc_time_t;

void rtc_init(void);
void rtc_read(rtc_time_t *out);

/* CPU identification */
void cpu_brand_string(char *out, size_t size);
void cpu_vendor_string(char *out, size_t size);

/* FPU / SSE */
/* Enables native x87 + SSE/SSE2 (CR0.EM/MP, CR4.OSFXSR/OSXMMEXCPT) and
 * resets the FPU to a safe default state. Returns false if the CPU lacks
 * FXSR/SSE2, in which case no SSE-compiled code may ever run. */
bool arch_fpu_init(void);

/* Reboot / power off */
NORETURN void arch_reboot(void);
NORETURN void arch_shutdown(void);
NORETURN void arch_halt(void);

#endif /* TINOS_ARCH_H */
