/* VGA text mode console used for the boot stage.
 *
 * The BIOS loader leaves the adapter in VGA text mode. The kernel prints
 * its start-up progress here before the VBE
 * driver switches the adapter into a linear framebuffer mode.
 */

#include <tinos/console.h>
#include <tinos/io.h>
#include <tinos/printf.h>
#include <tinos/string.h>

#define VGA_MEM    ((volatile uint16_t *)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25
#define CRTC_INDEX 0x3D4
#define CRTC_DATA  0x3D5

static uint8_t attr   = (VGA_BLACK << 4) | VGA_LGRAY;
static int     cursor_x;
static int     cursor_y;
static bool    active;

static void move_hw_cursor(void)
{
    uint16_t pos = (uint16_t)(cursor_y * VGA_COLS + cursor_x);
    outb(CRTC_INDEX, 0x0F);
    outb(CRTC_DATA, (uint8_t)(pos & 0xFF));
    outb(CRTC_INDEX, 0x0E);
    outb(CRTC_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_text_init(void)
{
    active = true;
    vga_text_set_color(VGA_LGRAY, VGA_BLACK);
    vga_text_clear();

    /* Enable a visible underline cursor (scanlines 13-14). */
    outb(CRTC_INDEX, 0x0A);
    outb(CRTC_DATA, 13);
    outb(CRTC_INDEX, 0x0B);
    outb(CRTC_DATA, 14);
}

void vga_text_disable(void)
{
    active = false;
}

bool vga_text_active(void)
{
    return active;
}

void vga_text_set_color(uint8_t fg, uint8_t bg)
{
    attr = (uint8_t)((bg << 4) | (fg & 0x0F));
}

void vga_text_clear(void)
{
    if (!active)
        return;
    uint16_t blank = (uint16_t)(' ' | (attr << 8));
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_MEM[i] = blank;
    cursor_x = cursor_y = 0;
    move_hw_cursor();
}

static void scroll(void)
{
    uint16_t blank = (uint16_t)(' ' | (attr << 8));
    for (int y = 1; y < VGA_ROWS; y++)
        for (int x = 0; x < VGA_COLS; x++)
            VGA_MEM[(y - 1) * VGA_COLS + x] = VGA_MEM[y * VGA_COLS + x];
    for (int x = 0; x < VGA_COLS; x++)
        VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + x] = blank;
    cursor_y = VGA_ROWS - 1;
}

void vga_text_putc(char c)
{
    if (!active)
        return;

    switch (c) {
    case '\n':
        cursor_x = 0;
        cursor_y++;
        break;
    case '\r':
        cursor_x = 0;
        break;
    case '\t':
        cursor_x = (cursor_x + 8) & ~7;
        break;
    case '\b':
        if (cursor_x > 0)
            cursor_x--;
        VGA_MEM[cursor_y * VGA_COLS + cursor_x] =
            (uint16_t)(' ' | (attr << 8));
        break;
    default:
        if ((uint8_t)c < 0x20)
            return;
        VGA_MEM[cursor_y * VGA_COLS + cursor_x] =
            (uint16_t)((uint8_t)c | (attr << 8));
        cursor_x++;
        break;
    }

    if (cursor_x >= VGA_COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    while (cursor_y >= VGA_ROWS)
        scroll();
    move_hw_cursor();
}

void vga_text_write(const char *s)
{
    while (*s)
        vga_text_putc(*s++);
}

void vga_text_printf(const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    vga_text_write(buf);
}
