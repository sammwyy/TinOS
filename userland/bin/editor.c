#include <tinos/string.h>
#include <tinos/ui.h>

#define DOCUMENT_CAP 2048

static tinos_ui_t ui;
static char document[DOCUMENT_CAP];
static char path[256] = "/users/sammy/note.txt";
static unsigned length, cursor;
static int modified;

static unsigned line_start(unsigned position)
{
    while (position && document[position - 1] != '\n')
        position--;
    return position;
}

static unsigned line_end(unsigned position)
{
    while (position < length && document[position] != '\n')
        position++;
    return position;
}

static void move_vertical(int down)
{
    unsigned start = line_start(cursor);
    unsigned column = cursor - start;
    unsigned next;
    if (down) {
        next = line_end(cursor);
        if (next == length)
            return;
        next++;
    } else {
        if (!start)
            return;
        next = line_start(start - 1);
    }
    unsigned end = line_end(next);
    cursor = next + (column < end - next ? column : end - next);
}

static void render(void)
{
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 0, 0, path);
    tinos_ui_text(&ui, 0, 1, "Ctrl+S Save   Esc Close");
    unsigned cursor_line = 0;
    for (unsigned i = 0; i < cursor; i++)
        if (document[i] == '\n')
            cursor_line++;
    unsigned visible = ui.rows - 4;
    unsigned first = cursor_line >= visible ? cursor_line - visible + 1 : 0;
    unsigned x = 0, y = 0;
    unsigned cursor_x = cursor - line_start(cursor);
    for (unsigned i = 0; i < length; i++) {
        if (document[i] == '\n') {
            x = 0;
            y++;
        } else {
            if (y >= first && y < first + visible && x < ui.columns)
                ui.cells[(2 + y - first) * ui.columns + x] =
                    document[i] >= 32 && document[i] < 127 ?
                    document[i] : ' ';
            x++;
        }
    }
    if (cursor_line >= first && cursor_line < first + visible &&
        cursor_x < ui.columns)
        ui.cells[(2 + cursor_line - first) * ui.columns + cursor_x] = '_';
    tinos_ui_text(&ui, 0, ui.rows - 1,
                  modified ? "Modified" : "Saved");
    tinos_ui_present(&ui);
}

static void save(void)
{
    int fd = tinos_open_flags(path, TINOS_O_WRONLY | TINOS_O_CREAT |
                                    TINOS_O_TRUNC);
    if (fd < 0)
        return;
    unsigned written = 0;
    while (written < length) {
        unsigned chunk = length - written > 1024 ? 1024 : length - written;
        int result = tinos_write(fd, document + written, chunk);
        if (result <= 0)
            break;
        written += (unsigned)result;
    }
    tinos_close(fd);
    if (written == length)
        modified = 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] == '/')
        tinos_strlcpy(path, argv[1], sizeof(path));
    int fd = tinos_open(path);
    if (fd >= 0) {
        int bytes;
        while (length + 1 < sizeof(document) &&
               (bytes = tinos_read(fd, document + length,
                          sizeof(document) - 1 - length > 1024 ?
                          1024 : sizeof(document) - 1 - length)) > 0)
            length += (unsigned)bytes;
        tinos_close(fd);
    }
    document[length] = '\0';
    cursor = length;
    if (tinos_ui_open(&ui, "Text Editor", 500, 320) < 0)
        return 1;
    render();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE)
            break;
        if (event.type != TINOS_GUI_KEY)
            continue;
        if (event.code == TINOS_KEY_ESCAPE)
            break;
        if ((event.mods & TINOS_MOD_CTRL) &&
            (event.ch == 's' || event.ch == 'S')) {
            save();
        } else if (event.code == TINOS_KEY_LEFT && cursor) {
            cursor--;
        } else if (event.code == TINOS_KEY_RIGHT && cursor < length) {
            cursor++;
        } else if (event.code == TINOS_KEY_UP) {
            move_vertical(0);
        } else if (event.code == TINOS_KEY_DOWN) {
            move_vertical(1);
        } else if (event.code == TINOS_KEY_BACKSPACE && cursor) {
            memmove(document + cursor - 1, document + cursor,
                    length - cursor + 1);
            cursor--;
            length--;
            modified = 1;
        } else if ((event.code == TINOS_KEY_ENTER ||
                    (event.ch >= 32 && event.ch < 127)) &&
                   length + 1 < DOCUMENT_CAP) {
            memmove(document + cursor + 1, document + cursor,
                    length - cursor + 1);
            document[cursor++] = event.code == TINOS_KEY_ENTER ?
                                 '\n' : (char)event.ch;
            length++;
            modified = 1;
        }
        render();
    }
    tinos_gui_close(ui.handle);
    return 0;
}
