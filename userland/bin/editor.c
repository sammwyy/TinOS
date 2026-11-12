#include <tinos/string.h>
#include <tinos/widgets.h>

#define DOCUMENT_CAP 2048

static tinos_widget_view_t ui;
static char document[DOCUMENT_CAP];
static char path[256] = "/users/sammy/note.txt";
static unsigned length, cursor;
static unsigned anchor, top_line;
static int modified, dragging, confirm_new;
static char message[64];

typedef struct {
    char text[DOCUMENT_CAP];
    unsigned length, cursor, anchor;
} edit_snapshot_t;

static edit_snapshot_t undo_stack[8], redo_stack[8];
static unsigned undo_count, redo_count;

static void remember(edit_snapshot_t stack[8], unsigned *count)
{
    if (*count == 8) {
        memmove(stack, stack + 1, 7 * sizeof(*stack));
        (*count)--;
    }
    edit_snapshot_t *state = &stack[(*count)++];
    memcpy(state->text, document, length + 1);
    state->length = length;
    state->cursor = cursor;
    state->anchor = anchor;
}

static void changed(void)
{
    remember(undo_stack, &undo_count);
    redo_count = 0;
    modified = 1;
    confirm_new = 0;
    message[0] = '\0';
}

static void restore(edit_snapshot_t stack[8], unsigned *count,
                    edit_snapshot_t other[8], unsigned *other_count)
{
    if (!*count)
        return;
    remember(other, other_count);
    edit_snapshot_t *state = &stack[--*count];
    length = state->length;
    memcpy(document, state->text, length + 1);
    cursor = state->cursor;
    anchor = state->anchor;
    modified = 1;
    message[0] = '\0';
}

static unsigned selection_start(void)
{
    return cursor < anchor ? cursor : anchor;
}

static unsigned selection_end(void)
{
    return cursor > anchor ? cursor : anchor;
}

static void remove_selection(void)
{
    unsigned start = selection_start(), end = selection_end();
    if (start == end)
        return;
    memmove(document + start, document + end, length - end + 1);
    length -= end - start;
    cursor = anchor = start;
}

static void insert_text(const char *text, unsigned size)
{
    if (size > DOCUMENT_CAP - 1 - length +
               (selection_end() - selection_start())) {
        tinos_strlcpy(message, "Document limit reached", sizeof(message));
        return;
    }
    changed();
    remove_selection();
    memmove(document + cursor + size, document + cursor,
            length - cursor + 1);
    memcpy(document + cursor, text, size);
    cursor += size;
    length += size;
    anchor = cursor;
}

static void copy_selection(void)
{
    unsigned start = selection_start(), size = selection_end() - start;
    if (size && tinos_clipboard_set(document + start, size,
                                    TINOS_CLIPBOARD_TEXT) < 0)
        tinos_strlcpy(message, "Clipboard unavailable", sizeof(message));
}

static void new_document(void)
{
    if (modified && !confirm_new) {
        confirm_new = 1;
        tinos_strlcpy(message, "Unsaved: New again to discard",
                      sizeof(message));
        return;
    }
    char name[11];
    char candidate[256];
    bool available = false;
    for (unsigned number = 1; number < 99; number++) {
        tinos_strlcpy(candidate, "/users/sammy/untitled",
                      sizeof(candidate));
        if (number > 1) {
            tinos_u32(name, number);
            tinos_strlcpy(candidate + strlen(candidate), name,
                          sizeof(candidate) - strlen(candidate));
        }
        tinos_strlcpy(candidate + strlen(candidate), ".txt",
                      sizeof(candidate) - strlen(candidate));
        tinos_dirent_t entry;
        if (tinos_stat(candidate, &entry) != 1) {
            available = true;
            break;
        }
    }
    if (!available) {
        tinos_strlcpy(message, "No available new filename", sizeof(message));
        return;
    }
    tinos_strlcpy(path, candidate, sizeof(path));
    length = cursor = anchor = top_line = 0;
    document[0] = '\0';
    undo_count = redo_count = 0;
    modified = 0;
    confirm_new = 0;
    message[0] = '\0';
}

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
    unsigned row = 0, start = 0;
    while (row < top_line && start < length) {
        if (document[start++] == '\n')
            row++;
    }
    char line[96];
    for (unsigned y = 0; y < visible && 28 + 8 + (int)y * 16 + 16 <= h - 22; y++) {
        unsigned end = line_end(start);
        if (anchor != cursor) {
            unsigned lo = selection_start(), hi = selection_end();
            if (lo < end && hi > start) {
                unsigned a = lo > start ? lo - start : 0;
                unsigned b = hi < end ? hi - start : end - start;
                if (b > columns) b = columns;
                if (a < b)
                    tinos_canvas_fill(canvas, 8 + a * 8,
                                      36 + (int)y * 16, (b - a) * 8,
                                      16, theme->selection);
            }
        }
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
    if (cursor_line >= top_line && cursor_line < top_line + visible &&
        cursor_x < columns)
        tinos_canvas_fill(canvas, 8 + (int)cursor_x * 8,
                          36 + (int)(cursor_line - top_line) * 16,
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
    if (message[0])
        tinos_canvas_text(canvas, 6, h - 19, w - 12, message, theme->danger);
    else {
        tinos_canvas_text(canvas, 6, h - 19, w - 116, status, theme->muted);
        tinos_canvas_text(canvas, w - 104, h - 19, 100,
                          "Ctrl+S save", theme->muted);
    }
}

static void follow_cursor(void)
{
    unsigned line = 0;
    for (unsigned i = 0; i < cursor; i++)
        if (document[i] == '\n')
            line++;
    unsigned visible = ui.height > 66 ?
        (ui.height - 28 - 22 - 16) / 16 : 1;
    if (line < top_line)
        top_line = line;
    else if (line >= top_line + visible)
        top_line = line - visible + 1;
}

static void render(void)
{
    follow_cursor();
    ui.dirty = true;
    tinos_widget_flush(&ui);
}

static void save(void)
{
    int fd = tinos_open_flags(path, TINOS_O_WRONLY | TINOS_O_CREAT |
                                    TINOS_O_TRUNC);
    if (fd < 0) {
        tinos_strlcpy(message, "Save failed", sizeof(message));
        return;
    }
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
    else
        tinos_strlcpy(message, "Save incomplete", sizeof(message));
}

static void toolbar_action(unsigned action)
{
    if (action == 0)
        save();
    else if (action == 1)
        new_document();
    else if (action == 2)
        restore(undo_stack, &undo_count, redo_stack, &redo_count);
    else if (action == 3)
        restore(redo_stack, &redo_count, undo_stack, &undo_count);
    else if (action == 4 || action == 5) {
        copy_selection();
        if (action == 4 && cursor != anchor) {
            changed();
            remove_selection();
        }
    } else if (action == 6) {
        char text[DOCUMENT_CAP];
        unsigned kind;
        int size = tinos_clipboard_get(text, sizeof(text), &kind);
        if (size > 0 && kind == TINOS_CLIPBOARD_TEXT)
            insert_text(text, (unsigned)size);
    }
    render();
}

static unsigned position_at(int x, int y)
{
    unsigned row = y > 36 ? (unsigned)(y - 36) / 16 : 0;
    unsigned line = top_line + row, offset = 0;
    while (line-- && offset < length) {
        offset = line_end(offset);
        if (offset < length)
            offset++;
    }
    unsigned end = line_end(offset);
    unsigned column = x > 8 ? (unsigned)(x - 8) / 8 : 0;
    return offset + (column < end - offset ? column : end - offset);
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
    anchor = cursor;
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
        if (event.type == TINOS_GUI_MOUSE && (event.code & 1) &&
            event.y >= 3 && event.y < 25 && event.x >= 4) {
            int x = 4;
            for (unsigned i = 0; i < 7; i++) {
                int width = i ? 52 : 56;
                if (event.x >= x && event.x < x + width) {
                    toolbar_action(i);
                    break;
                }
                x += width + 3;
            }
            continue;
        }
        if (event.type == TINOS_GUI_MOUSE &&
            event.y >= 28 && event.y < (int)ui.height - 22) {
            if (event.code & TINOS_GUI_MOUSE_LEFT) {
                cursor = anchor = position_at(event.x, event.y);
                dragging = 1;
                render();
            } else if (dragging && (event.mods & TINOS_GUI_MOUSE_LEFT)) {
                cursor = position_at(event.x, event.y);
                render();
            } else if (event.mods >> 8 & TINOS_GUI_MOUSE_LEFT) {
                dragging = 0;
                cursor = position_at(event.x, event.y);
                render();
            }
            continue;
        }
        if (event.type == TINOS_GUI_MOUSE && (int32_t)event.ch) {
            int step = (int32_t)event.ch > 0 ? -3 : 3;
            int next = (int)top_line + step;
            top_line = next > 0 ? (unsigned)next : 0;
            ui.dirty = true;
            tinos_widget_flush(&ui);
            continue;
        }
        if (event.type != TINOS_GUI_KEY)
            continue;
        if (event.code == TINOS_KEY_ESCAPE)
            break;
        if (event.mods & TINOS_MOD_CTRL) {
            char key = event.ch >= 'A' && event.ch <= 'Z' ?
                       (char)(event.ch - 'A' + 'a') : (char)event.ch;
            if (key == 'a') {
                anchor = 0;
                cursor = length;
            } else if (key == 's') {
                save();
            } else if (key == 'n') {
                new_document();
            } else if (key == 'z') {
                toolbar_action(2);
            } else if (key == 'y') {
                toolbar_action(3);
            } else if (key == 'c') {
                toolbar_action(5);
            } else if (key == 'x') {
                toolbar_action(4);
            } else if (key == 'v') {
                toolbar_action(6);
            }
        } else if (event.code == TINOS_KEY_LEFT && cursor) {
            cursor--;
        } else if (event.code == TINOS_KEY_RIGHT && cursor < length) {
            cursor++;
        } else if (event.code == TINOS_KEY_UP) {
            move_vertical(0);
        } else if (event.code == TINOS_KEY_DOWN) {
            move_vertical(1);
        } else if (event.code == TINOS_KEY_PAGEUP ||
                   event.code == TINOS_KEY_PAGEDOWN) {
            unsigned rows = ui.height > 66 ?
                            (ui.height - 66) / 16 : 1;
            for (unsigned i = 0; i < rows; i++)
                move_vertical(event.code == TINOS_KEY_PAGEDOWN);
        } else if (event.code == TINOS_KEY_HOME) {
            cursor = line_start(cursor);
        } else if (event.code == TINOS_KEY_END) {
            cursor = line_end(cursor);
        } else if (event.code == TINOS_KEY_BACKSPACE ||
                   event.code == TINOS_KEY_DELETE) {
            if (anchor != cursor) {
                changed();
                remove_selection();
            } else if ((event.code == TINOS_KEY_BACKSPACE && cursor) ||
                       (event.code == TINOS_KEY_DELETE && cursor < length)) {
                changed();
                unsigned at = event.code == TINOS_KEY_BACKSPACE ?
                              cursor - 1 : cursor;
                memmove(document + at, document + at + 1,
                        length - at);
                cursor = anchor = at;
                length--;
            }
        } else if ((event.code == TINOS_KEY_ENTER ||
                    (event.ch >= 32 && event.ch < 127)) &&
                   length + 1 < DOCUMENT_CAP) {
            char ch = event.code == TINOS_KEY_ENTER ? '\n' : (char)event.ch;
            insert_text(&ch, 1);
        } else if (event.code == TINOS_KEY_TAB) {
            insert_text("    ", 4);
        }
        if (!(event.mods & TINOS_MOD_SHIFT) &&
            !(event.mods & TINOS_MOD_CTRL))
            anchor = cursor;
        render();
    }
    tinos_widget_close(&ui);
    return 0;
}
