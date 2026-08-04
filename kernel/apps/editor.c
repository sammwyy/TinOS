/* Text editor / viewer.
 *
 * Loads a file from the in-memory filesystem into an editable buffer and
 * writes it back with Ctrl+S.  Opened by the file explorer (Enter on a file)
 * and by the `edit` shell command.
 */

#include <tinos/arch.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/path.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define PADDING          6
#define STATUS_H         20

typedef struct {
    char   path[FS_PATH_MAX];
    char  *buffer;
    size_t length;
    size_t capacity;
    size_t cursor;
    size_t top_line;
    size_t left_column;
    bool   modified;
    int    cols, rows;
    bool   blink_on;
    uint64_t blink_at;
} editor_t;

/* ------------------------------------------------------------------ */
/* Buffer helpers                                                      */
/* ------------------------------------------------------------------ */

static bool ensure_capacity(editor_t *ed, size_t needed)
{
    if (ed->capacity >= needed)
        return true;
    size_t capacity = ed->capacity ? ed->capacity : 256;
    while (capacity < needed)
        capacity *= 2;
    char *buf = krealloc(ed->buffer, capacity);
    if (!buf)
        return false;
    ed->buffer   = buf;
    ed->capacity = capacity;
    return true;
}

static size_t line_start(const editor_t *ed, size_t offset)
{
    while (offset > 0 && ed->buffer[offset - 1] != '\n')
        offset--;
    return offset;
}

static size_t line_end(const editor_t *ed, size_t offset)
{
    while (offset < ed->length && ed->buffer[offset] != '\n')
        offset++;
    return offset;
}

static size_t line_number(const editor_t *ed, size_t offset)
{
    size_t line = 0;
    for (size_t i = 0; i < offset && i < ed->length; i++)
        if (ed->buffer[i] == '\n')
            line++;
    return line;
}

static size_t offset_of_line(const editor_t *ed, size_t line)
{
    size_t current = 0;
    for (size_t i = 0; i < ed->length; i++) {
        if (current == line)
            return i;
        if (ed->buffer[i] == '\n')
            current++;
    }
    return current == line ? ed->length : ed->length;
}

static size_t total_lines(const editor_t *ed)
{
    size_t lines = 1;
    for (size_t i = 0; i < ed->length; i++)
        if (ed->buffer[i] == '\n')
            lines++;
    return lines;
}

static void insert_text(editor_t *ed, const char *text, size_t len)
{
    if (!ensure_capacity(ed, ed->length + len + 1))
        return;
    memmove(ed->buffer + ed->cursor + len, ed->buffer + ed->cursor,
            ed->length - ed->cursor);
    memcpy(ed->buffer + ed->cursor, text, len);
    ed->length += len;
    ed->cursor += len;
    ed->buffer[ed->length] = '\0';
    ed->modified           = true;
}

static void delete_range(editor_t *ed, size_t start, size_t count)
{
    if (start >= ed->length || count == 0)
        return;
    count = MIN(count, ed->length - start);
    memmove(ed->buffer + start, ed->buffer + start + count,
            ed->length - start - count);
    ed->length -= count;
    ed->buffer[ed->length] = '\0';
    ed->cursor             = start;
    ed->modified           = true;
}

/* ------------------------------------------------------------------ */
/* Load and save                                                       */
/* ------------------------------------------------------------------ */

static void editor_update_title(window_t *win, editor_t *ed)
{
    char title[64];
    ksnprintf(title, sizeof(title), "%s%s - Editor", ed->modified ? "*" : "",
              path_basename(ed->path));
    wm_set_title(win, title);
}

static bool editor_load(editor_t *ed)
{
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), ed->path, &node) != FS_OK)
        return false;
    if (node->type != FS_FILE)
        return false;

    size_t len = fs_size(node);
    if (!ensure_capacity(ed, len + 1))
        return false;
    if (len)
        memcpy(ed->buffer, fs_data(node), len);
    ed->length         = len;
    ed->buffer[len]    = '\0';
    ed->cursor         = 0;
    ed->modified       = false;
    return true;
}

static void editor_save(window_t *win, editor_t *ed)
{
    fs_node_t  *node = NULL;
    fs_result_t r    = fs_resolve(fs_root(), ed->path, &node);

    if (r == FS_ERR_NOENT)
        r = fs_create(fs_root(), ed->path, &node);
    if (r != FS_OK) {
        desktop_notify("Save failed: %s", fs_strerror(r));
        return;
    }
    r = fs_write(node, ed->buffer, ed->length);
    if (r != FS_OK) {
        desktop_notify("Save failed: %s", fs_strerror(r));
        return;
    }
    ed->modified = false;
    editor_update_title(win, ed);
    desktop_notify("Saved %s (%u bytes)", ed->path, (uint32_t)ed->length);
}

/* ------------------------------------------------------------------ */
/* Application class                                                   */
/* ------------------------------------------------------------------ */

static void editor_on_open(window_t *win, const char *arg)
{
    editor_t *ed = kcalloc(1, sizeof(editor_t));
    if (!ed) {
        klog("[edit] out of memory\n");
        return;
    }
    win->priv = ed;
    path_normalize(arg && arg[0] ? arg : "/README.txt", ed->path,
                   sizeof(ed->path));

    rect_t client = wm_client_rect(win);
    ed->cols      = (client.w - 2 * PADDING) / FONT_W;
    ed->rows      = (client.h - STATUS_H - 2 * PADDING) / FONT_H;

    if (!editor_load(ed)) {
        if (!ensure_capacity(ed, 64)) {
            klog("[edit] out of memory for the edit buffer\n");
            return;
        }
        ed->length    = 0;
        ed->buffer[0] = '\0';
        desktop_notify("%s could not be opened", ed->path);
    }
    editor_update_title(win, ed);
}

static void editor_on_close(window_t *win)
{
    editor_t *ed = win->priv;
    if (!ed)
        return;
    kfree(ed->buffer);
    kfree(ed);
    win->priv = NULL;
}

static void editor_scroll_to_cursor(editor_t *ed)
{
    size_t line = line_number(ed, ed->cursor);
    if (line < ed->top_line)
        ed->top_line = line;
    else if (line >= ed->top_line + (size_t)ed->rows)
        ed->top_line = line - (size_t)ed->rows + 1;

    size_t column = ed->cursor - line_start(ed, ed->cursor);
    if (column < ed->left_column)
        ed->left_column = column;
    else if (column >= ed->left_column + (size_t)ed->cols)
        ed->left_column = column - (size_t)ed->cols + 1;
}

static void editor_on_draw(window_t *win, rect_t client)
{
    editor_t *ed = win->priv;
    if (!ed)
        return;

    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_TERMINAL_BG);

    int x = client.x + PADDING;
    int y = client.y + PADDING;

    size_t offset = offset_of_line(ed, ed->top_line);
    for (int row = 0; row < ed->rows; row++) {
        if (offset > ed->length)
            break;
        size_t end = line_end(ed, offset);
        size_t len = end - offset;

        if (len > ed->left_column) {
            size_t visible = MIN(len - ed->left_column, (size_t)ed->cols);
            gfx_text_n(x, y + row * FONT_H, ed->buffer + offset +
                       ed->left_column, visible, COLOR_TERMINAL_TEXT);
        }
        if (end >= ed->length)
            break;
        offset = end + 1;
    }

    /* Cursor */
    if (ed->blink_on && win == wm_focused()) {
        size_t line   = line_number(ed, ed->cursor);
        size_t column = ed->cursor - line_start(ed, ed->cursor);
        if (line >= ed->top_line && line < ed->top_line + (size_t)ed->rows &&
            column >= ed->left_column) {
            int cx = x + (int)(column - ed->left_column) * FONT_W;
            int cy = y + (int)(line - ed->top_line) * FONT_H;
            gfx_fill_rect(cx, cy, 2, FONT_H, COLOR_TERMINAL_PROMPT);
        }
    }

    /* Status line */
    rect_t status = rect_make(client.x, client.y + client.h - STATUS_H,
                              client.w, STATUS_H);
    gfx_fill_rect(status.x, status.y, status.w, status.h, COLOR_PANEL);
    gfx_hline(status.x, status.y, status.w, COLOR_BORDER);

    char left[160];
    ksnprintf(left, sizeof(left), "%s%s  line %u/%u  col %u  %u bytes",
              ed->modified ? "*" : " ", ed->path,
              (uint32_t)(line_number(ed, ed->cursor) + 1),
              (uint32_t)total_lines(ed),
              (uint32_t)(ed->cursor - line_start(ed, ed->cursor) + 1),
              (uint32_t)ed->length);
    gfx_clip_push(status);
    gfx_text(status.x + 6, status.y + (STATUS_H - FONT_H) / 2, left,
             COLOR_TEXT_DIM);
    const char *hint = "Ctrl+S save";
    gfx_text(status.x + status.w - gfx_text_width(hint) - 8,
             status.y + (STATUS_H - FONT_H) / 2, hint, COLOR_TEXT_DIM);
    gfx_clip_pop();
}

static bool editor_on_key(window_t *win, const key_event_t *ev)
{
    editor_t *ed = win->priv;
    if (!ed)
        return false;

    if ((ev->mods & KMOD_CTRL) && (ev->ch == 's' || ev->ch == 'S')) {
        editor_save(win, ed);
        wm_invalidate(win);
        return true;
    }

    switch (ev->code) {
    case KEY_LEFT:
        if (ed->cursor)
            ed->cursor--;
        break;
    case KEY_RIGHT:
        if (ed->cursor < ed->length)
            ed->cursor++;
        break;
    case KEY_UP:
    case KEY_DOWN: {
        size_t line   = line_number(ed, ed->cursor);
        size_t column = ed->cursor - line_start(ed, ed->cursor);
        if (ev->code == KEY_UP && line == 0)
            break;
        size_t target = ev->code == KEY_UP ? line - 1 : line + 1;
        if (target >= total_lines(ed))
            break;
        size_t start = offset_of_line(ed, target);
        size_t end   = line_end(ed, start);
        ed->cursor   = MIN(start + column, end);
        break;
    }
    case KEY_HOME:
        ed->cursor = line_start(ed, ed->cursor);
        break;
    case KEY_END:
        ed->cursor = line_end(ed, ed->cursor);
        break;
    case KEY_PAGEUP: {
        size_t line   = line_number(ed, ed->cursor);
        size_t target = line > (size_t)ed->rows ? line - (size_t)ed->rows : 0;
        ed->cursor    = offset_of_line(ed, target);
        break;
    }
    case KEY_PAGEDOWN: {
        size_t line   = line_number(ed, ed->cursor) + (size_t)ed->rows;
        size_t last   = total_lines(ed) - 1;
        ed->cursor    = offset_of_line(ed, MIN(line, last));
        break;
    }
    case KEY_BACKSPACE:
        if (ed->cursor > 0)
            delete_range(ed, ed->cursor - 1, 1);
        break;
    case KEY_DELETE:
        delete_range(ed, ed->cursor, 1);
        break;
    case KEY_ENTER:
        insert_text(ed, "\n", 1);
        break;
    case KEY_TAB:
        insert_text(ed, "    ", 4);
        break;
    default:
        if (ev->ch >= 0x20 && ev->ch < 0x7F) {
            char c = ev->ch;
            insert_text(ed, &c, 1);
        } else {
            return false;
        }
        break;
    }

    editor_scroll_to_cursor(ed);
    editor_update_title(win, ed);
    ed->blink_on = true;
    wm_invalidate(win);
    return true;
}

static bool editor_on_tick(window_t *win)
{
    editor_t *ed = win->priv;
    if (!ed)
        return false;
    uint64_t now = timer_ticks();
    if (now >= ed->blink_at) {
        ed->blink_on = !ed->blink_on;
        ed->blink_at = now + 500;
        return win == wm_focused();
    }
    return false;
}

static const app_class_t editor_class = {
    .name     = "editor",
    .on_open  = editor_on_open,
    .on_close = editor_on_close,
    .on_draw  = editor_on_draw,
    .on_key   = editor_on_key,
    .on_tick  = editor_on_tick,
};

window_t *app_editor_open(const char *path)
{
    return wm_open(&editor_class, 660, 400, "Editor", path);
}
