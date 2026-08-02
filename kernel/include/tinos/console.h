/* Boot-stage VGA text console (80x25 at 0xB8000) and the serial log. */
#ifndef TINOS_CONSOLE_H
#define TINOS_CONSOLE_H

#include <tinos/kernel.h>
#include <tinos/types.h>

/* Standard CGA/EGA palette entries. */
enum {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA,
    VGA_BROWN, VGA_LGRAY, VGA_DGRAY, VGA_LBLUE, VGA_LGREEN, VGA_LCYAN,
    VGA_LRED, VGA_LMAGENTA, VGA_YELLOW, VGA_WHITE
};

void vga_text_init(void);
void vga_text_clear(void);
void vga_text_set_color(uint8_t fg, uint8_t bg);
void vga_text_putc(char c);
void vga_text_write(const char *s);
void vga_text_printf(const char *fmt, ...) PRINTFLIKE(1, 2);
bool vga_text_active(void);
void vga_text_disable(void); /* called when we leave text mode */

/* COM1 serial log, always available (QEMU: -serial stdio/file). */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif /* TINOS_CONSOLE_H */
