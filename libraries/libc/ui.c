#include <tinos/string.h>
#include <tinos/ui.h>

int tinos_ui_open(tinos_ui_t *ui, const char *title,
                  unsigned width, unsigned height)
{
    if (!ui || width < 180 || height < 120)
        return -1;
    ui->handle = tinos_gui_open(title, width, height);
    if (ui->handle < 0)
        return -1;
    ui->columns = (width - 4) / 8;
    ui->rows = (height - 22) / 16;
    if (ui->columns > TINOS_GUI_COLS)
        ui->columns = TINOS_GUI_COLS;
    if (ui->rows > TINOS_GUI_ROWS)
        ui->rows = TINOS_GUI_ROWS;
    tinos_ui_clear(ui);
    return 0;
}

void tinos_ui_clear(tinos_ui_t *ui)
{
    memset(ui->cells, ' ', ui->columns * ui->rows);
    ui->x = ui->y = 0;
}

void tinos_ui_text(tinos_ui_t *ui, unsigned x, unsigned y, const char *text)
{
    if (y >= ui->rows || x >= ui->columns)
        return;
    while (*text && x < ui->columns)
        ui->cells[y * ui->columns + x++] = *text++;
}

void tinos_ui_puts(tinos_ui_t *ui, const char *text)
{
    for (; *text; text++) {
        if (*text == '\n' || ui->x == ui->columns) {
            ui->x = 0;
            ui->y++;
            if (ui->y == ui->rows) {
                memmove(ui->cells, ui->cells + ui->columns,
                        (ui->rows - 1) * ui->columns);
                memset(ui->cells + (ui->rows - 1) * ui->columns,
                       ' ', ui->columns);
                ui->y--;
            }
        }
        if (*text != '\n')
            ui->cells[ui->y * ui->columns + ui->x++] = *text;
    }
}

int tinos_ui_present(tinos_ui_t *ui)
{
    return tinos_gui_present(ui->handle, ui->cells, ui->columns, ui->rows);
}
