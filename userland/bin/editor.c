#include <tinos/string.h>
#include <tinos/widgets.h>

#define DOCUMENT_CAP 2048

static tinos_widget_view_t ui;
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

static const char *const toolbar_icons[] = {
    "save", "new", "undo", "redo", "file", "editor", "file",
};
static const char *const toolbar_labels[] = {
    "Save", "New", "Undo", "Redo", "Cut", "Copy", "Paste",
};

static void append(char *target, size_t cap, const char *part)
{
    size_t used = strlen(target);
    if (used < cap)
        tinos_strlcpy(target + used, part, cap - used);
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    tinos_canvas_fill(canvas, 0, 0, w, h, 0xf0eff4u);
    tinos_canvas_fill(canvas, 0, 0, w, 28, theme->panel);
    tinos_canvas_fill(canvas, 0, 27, w, 1, theme->border);
    int button_x = 4;
    for (unsigned i = 0; i < 7; i++) {
        int button_w = i ? 52 : 56;
        if (button_x + button_w > w - 4)
            break;
        tinos_canvas_button(canvas, button_x, 3, button_w, 22, NULL, false);
        tinos_canvas_icon(canvas, toolbar_icons[i], button_x + 3, 6, 16);
        tinos_canvas_text(canvas, button_x + 22, 6, button_w - 24,
                          toolbar_labels[i], theme->foreground);
        button_x += button_w + 3;
    }
    unsigned cursor_line = 0;
    for (unsigned i = 0; i < cursor; i++)
        if (document[i] == '\n')
            cursor_line++;
    unsigned visible = h > 66 ? (h - 28 - 22 - 16) / 16 : 1;
    unsigned columns = w > 16 ? (w - 16) / 8 : 1;
    unsigned first = cursor_line >= visible ? cursor_line - visible + 1 : 0;
    unsigned row = 0, start = 0;
    while (row < first && start < length) {
        if (document[start++] == '\n')
            row++;
    }
    char line[96];
    for (unsigned y = 0; y < visible && 28 + 8 + (int)y * 16 + 16 <= h - 22; y++) {
        unsigned end = line_end(start);
        unsigned n = end - start;
        if (n > columns)
            n = columns;
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        for (unsigned x = 0; x < n; x++)
            line[x] = document[start + x] >= 32 && document[start + x] < 127 ?
                      document[start + x] : ' ';
        line[n] = '\0';
        tinos_canvas_text(canvas, 8, 36 + (int)y * 16, w - 16,
                          line, theme->foreground);
        if (end == length)
            break;
        start = end + 1;
    }
    unsigned cursor_x = cursor - line_start(cursor);
    if (cursor_line >= first && cursor_line < first + visible &&
        cursor_x < columns)
        tinos_canvas_fill(canvas, 8 + (int)cursor_x * 8,
                          36 + (int)(cursor_line - first) * 16,
                          2, 16, theme->accent_light);

    tinos_canvas_fill(canvas, 0, h - 22, w, 22, theme->panel);
    tinos_canvas_fill(canvas, 0, h - 22, w, 1, theme->border);
    unsigned total_lines = 1;
    for (unsigned i = 0; i < length; i++)
        if (document[i] == '\n')
            total_lines++;
    char status[320] = "";
    char number[11];
    append(status, sizeof(status), modified ? "*" : " ");
    append(status, sizeof(status), path);
    append(status, sizeof(status), "  line ");
    tinos_u32(number, cursor_line + 1);
    append(status, sizeof(status), number);
    append(status, sizeof(status), "/");
    tinos_u32(number, total_lines);
    append(status, sizeof(status), number);
    append(status, sizeof(status), "  col ");
    tinos_u32(number, cursor_x + 1);
    append(status, sizeof(status), number);
    append(status, sizeof(status), "  ");
    tinos_u32(number, length);
    append(status, sizeof(status), number);
    append(status, sizeof(status), " bytes");
    tinos_canvas_text(canvas, 6, h - 19, w - 116, status, theme->muted);
    tinos_canvas_text(canvas, w - 104, h - 19, 100,
                      "Ctrl+S save", theme->muted);
}

static void render(void)
{
    ui.dirty = true;
    tinos_widget_flush(&ui);
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
    tinos_system_info_t info;
    bool roomy = tinos_sysinfo(&info) == 0 &&
                 info.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Text Editor",
                          roomy ? 660 : 500, roomy ? 400 : 320) < 0)
        return 1;
    ui.on_paint = paint;
    render();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE)
            break;
        if (event.type == TINOS_GUI_RESIZE) {
            render();
            continue;
        }
        if (event.type == TINOS_GUI_MOUSE && event.code == 1 &&
            event.y >= 3 && event.y < 25 && event.x >= 4 &&
            event.x < 60) {
            save();
            render();
            continue;
        }
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
    tinos_widget_close(&ui);
    return 0;
}
