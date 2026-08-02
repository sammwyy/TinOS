/* Fatal error reporting.
 *
 * A panic must be visible no matter which stage of boot we are in, so the
 * message is written to the serial port, to the VGA text console if we are
 * still in text mode, and painted directly into the framebuffer once the
 * graphics stack is up.
 */

#include <tinos/arch.h>
#include <tinos/console.h>
#include <tinos/gfx.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>

static bool panicking;

static void draw_graphical_panic(const char *message)
{
    const color_t bg    = RGB(20, 30, 90);
    const color_t fg    = RGB(235, 235, 245);
    const color_t badge = RGB(220, 70, 70);

    gfx_clip_reset();
    gfx_clear(bg);

    int x = 48, y = 48;
    gfx_fill_rect(x, y, 220, 28, badge);
    gfx_text(x + 10, y + 6, "KERNEL PANIC", RGB(255, 255, 255));
    y += 56;

    gfx_text(x, y, TINOS_NAME " " TINOS_VERSION " has stopped to avoid damage.",
             fg);
    y += FONT_H * 2;

    /* Wrap the message on newlines and on the screen width. */
    int         max_cols = (gfx_width() - 2 * x) / FONT_W;
    const char *p        = message;
    while (*p && y < gfx_height() - FONT_H * 3) {
        int len = 0;
        while (p[len] && p[len] != '\n' && len < max_cols)
            len++;
        gfx_text_n(x, y, p, (size_t)len, fg);
        y += FONT_H;
        p += len;
        if (*p == '\n')
            p++;
    }

    y += FONT_H;
    gfx_text(x, y, "The system has been halted. Reset this machine.",
             RGB(180, 190, 220));
    gfx_mark_all_dirty();
    gfx_present();
}

NORETURN void panic(const char *fmt, ...)
{
    char    message[1024];
    va_list ap;

    cli();

    if (panicking) {
        /* A fault inside the panic path: stop immediately. */
        serial_write("\n[panic] recursive panic, halting\n");
        arch_halt();
    }
    panicking = true;

    va_start(ap, fmt);
    kvsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    serial_write("\n*** KERNEL PANIC ***\n");
    serial_write(message);
    serial_write("\n*** system halted ***\n");

    if (gfx_active()) {
        draw_graphical_panic(message);
    } else if (vga_text_active()) {
        vga_text_set_color(VGA_WHITE, VGA_RED);
        vga_text_clear();
        vga_text_write("*** KERNEL PANIC ***\n\n");
        vga_text_write(message);
        vga_text_write("\n\nThe system has been halted.");
    }

    arch_halt();
}
