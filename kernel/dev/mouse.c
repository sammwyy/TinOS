/* PS/2 mouse driver.
 *
 * The auxiliary device is enabled through the 8042 controller and delivers
 * three byte packets on IRQ12.  The driver keeps an absolute cursor
 * position clamped to the screen and queues movement/button events for the
 * window manager.
 */

#include <tinos/arch.h>
#include <tinos/input.h>
#include <tinos/io.h>
#include <tinos/kernel.h>

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define QUEUE_SIZE 64

static volatile mouse_event_t queue[QUEUE_SIZE];
static volatile uint32_t      q_head, q_tail;

static uint8_t packet[3];
static int     packet_index;
static int     cursor_x, cursor_y;
static int     bound_w = 1024, bound_h = 768;
static uint8_t buttons;
static bool    present;

static bool wait_write(void)
{
    for (int i = 0; i < 100000; i++)
        if ((inb(PS2_STATUS) & 0x02) == 0)
            return true;
    return false;
}

static bool wait_read(void)
{
    for (int i = 0; i < 100000; i++)
        if (inb(PS2_STATUS) & 0x01)
            return true;
    return false;
}

static void ctrl_command(uint8_t cmd)
{
    wait_write();
    outb(PS2_CMD, cmd);
}

static bool mouse_command(uint8_t cmd)
{
    ctrl_command(0xD4); /* next byte goes to the auxiliary device */
    if (!wait_write())
        return false;
    outb(PS2_DATA, cmd);
    if (!wait_read())
        return false;
    return inb(PS2_DATA) == 0xFA; /* ACK */
}

static void push_event(const mouse_event_t *ev)
{
    uint32_t next = (q_head + 1) % QUEUE_SIZE;
    if (next == q_tail)
        return;
    queue[q_head] = *ev;
    q_head        = next;
}

static void mouse_isr(regs_t *regs)
{
    UNUSED(regs);

    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x01) || !(status & 0x20))
        return; /* not mouse data */

    uint8_t byte = inb(PS2_DATA);

    /* Bit 3 of the first byte is always set; use it to resynchronise. */
    if (packet_index == 0 && !(byte & 0x08))
        return;

    packet[packet_index++] = byte;
    if (packet_index < 3)
        return;
    packet_index = 0;

    uint8_t flags = packet[0];
    if (flags & 0xC0)
        return; /* X or Y overflow: discard the packet */

    int dx = (int)packet[1];
    int dy = (int)packet[2];
    if (flags & 0x10)
        dx -= 256;
    if (flags & 0x20)
        dy -= 256;

    int old_x = cursor_x, old_y = cursor_y;
    cursor_x += dx;
    cursor_y -= dy; /* PS/2 Y grows upwards, the screen grows downwards */
    cursor_x = CLAMP(cursor_x, 0, bound_w - 1);
    cursor_y = CLAMP(cursor_y, 0, bound_h - 1);

    uint8_t new_buttons = flags & 0x07;
    mouse_event_t ev    = {
        .x        = cursor_x,
        .y        = cursor_y,
        .dx       = cursor_x - old_x,
        .dy       = cursor_y - old_y,
        .buttons  = new_buttons,
        .pressed  = (uint8_t)(new_buttons & ~buttons),
        .released = (uint8_t)(buttons & ~new_buttons),
        .moved    = (cursor_x != old_x || cursor_y != old_y),
    };
    buttons = new_buttons;

    if (ev.moved || ev.pressed || ev.released)
        push_event(&ev);
}

static bool mouse_setup(void)
{
    /* Drain anything stale before talking to the controller. */
    for (int i = 0; i < 32 && (inb(PS2_STATUS) & 0x01); i++)
        (void)inb(PS2_DATA);

    ctrl_command(0xA8); /* enable the auxiliary device */

    /* Turn on IRQ12 in the controller configuration byte. */
    ctrl_command(0x20);
    if (!wait_read()) {
        klog("[mous] controller did not return its configuration byte\n");
        return false;
    }
    uint8_t config = inb(PS2_DATA);
    config |= 0x02;           /* auxiliary interrupt enable */
    config &= (uint8_t)~0x20; /* auxiliary clock enable     */
    ctrl_command(0x60);
    if (!wait_write()) {
        klog("[mous] controller would not accept a new configuration\n");
        return false;
    }
    outb(PS2_DATA, config);

    if (!mouse_command(0xF6)) { /* restore defaults */
        klog("[mous] device did not acknowledge 'set defaults'\n");
        return false;
    }
    if (!mouse_command(0xF4)) { /* enable data reporting */
        klog("[mous] device did not acknowledge 'enable reporting'\n");
        return false;
    }
    return true;
}

bool mouse_init(void)
{
    q_head = q_tail = 0;
    packet_index    = 0;
    buttons         = 0;
    cursor_x        = bound_w / 2;
    cursor_y        = bound_h / 2;

    /* The 8042 delivers command responses through the same output buffer the
     * keyboard uses, so the keyboard interrupt handler must not be allowed to
     * steal them while we are configuring the auxiliary device. */
    pic_mask_irq(1);
    bool saved = irq_save();

    bool ok = mouse_setup();

    irq_restore(saved);
    pic_unmask_irq(1);

    if (!ok)
        return false;

    present = true;
    irq_install(12, mouse_isr);
    return true;
}

bool mouse_present(void)
{
    return present;
}

void mouse_set_bounds(int width, int height)
{
    bound_w  = width;
    bound_h  = height;
    cursor_x = CLAMP(cursor_x, 0, bound_w - 1);
    cursor_y = CLAMP(cursor_y, 0, bound_h - 1);
}

bool mouse_poll(mouse_event_t *out)
{
    bool saved = irq_save();
    bool got   = false;
    if (q_tail != q_head) {
        *out   = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE_SIZE;
        got    = true;
    }
    irq_restore(saved);
    return got;
}

void mouse_get_position(int *x, int *y)
{
    bool saved = irq_save();
    *x         = cursor_x;
    *y         = cursor_y;
    irq_restore(saved);
}

uint8_t mouse_buttons(void)
{
    return buttons;
}
