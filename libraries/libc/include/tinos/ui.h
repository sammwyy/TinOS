#ifndef TINOS_LIBC_UI_H
#define TINOS_LIBC_UI_H

#include <tinos/libc.h>

typedef struct {
    int handle;
    unsigned columns, rows;
    unsigned x, y;
    char cells[TINOS_GUI_TEXT_SIZE];
} tinos_ui_t;

int tinos_ui_open(tinos_ui_t *ui, const char *title,
                  unsigned width, unsigned height);
void tinos_ui_clear(tinos_ui_t *ui);
void tinos_ui_text(tinos_ui_t *ui, unsigned x, unsigned y, const char *text);
void tinos_ui_puts(tinos_ui_t *ui, const char *text);
int tinos_ui_present(tinos_ui_t *ui);

#endif
