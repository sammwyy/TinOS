/* 8259A programmable interrupt controller. */

#include <tinos/arch.h>
#include <tinos/io.h>

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x11 /* init + expect ICW4 */
#define ICW4_8086 0x01

static uint16_t pic_masks = 0xFFFF; /* everything masked after remap */

void pic_remap(int master_offset, int slave_offset)
{
    outb(PIC1_CMD, ICW1_INIT);
    io_wait();
    outb(PIC2_CMD, ICW1_INIT);
    io_wait();
    outb(PIC1_DATA, (uint8_t)master_offset);
    io_wait();
    outb(PIC2_DATA, (uint8_t)slave_offset);
    io_wait();
    outb(PIC1_DATA, 0x04); /* slave attached to IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02); /* slave identity */
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /* Mask everything; drivers unmask their own line via irq_install(). */
    pic_masks = 0xFFFF;
    outb(PIC1_DATA, (uint8_t)(pic_masks & 0xFF));
    outb(PIC2_DATA, (uint8_t)(pic_masks >> 8));
}

void pic_mask_irq(int irq)
{
    if (irq < 0 || irq >= 16)
        return;
    pic_masks |= (uint16_t)(1u << irq);
    if (irq < 8)
        outb(PIC1_DATA, (uint8_t)(pic_masks & 0xFF));
    else
        outb(PIC2_DATA, (uint8_t)(pic_masks >> 8));
}

void pic_unmask_irq(int irq)
{
    if (irq < 0 || irq >= 16)
        return;
    pic_masks &= (uint16_t)~(1u << irq);
    if (irq >= 8) {
        /* The cascade line must be open for any slave interrupt. */
        pic_masks &= (uint16_t)~(1u << 2);
        outb(PIC2_DATA, (uint8_t)(pic_masks >> 8));
    }
    outb(PIC1_DATA, (uint8_t)(pic_masks & 0xFF));
}

void pic_send_eoi(int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
