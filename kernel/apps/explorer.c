/* File Explorer.
 *
 * Browses the same node tree the shell works on, so anything a terminal does
 * shows up here (and vice versa) - the view is rebuilt whenever the
 * filesystem generation counter changes.
 *
 * Copy and paste operate on real filesystem objects through fs_copy_node(),
 * not on list entries.
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

#define TOOLBAR_H    28
#define PATHBAR_H    24
#define STATUS_H     22
#define ROW_H        20

typedef enum {
    MODE_BROWSE = 0,
    MODE_NEW_DIR,
    MODE_NEW_FILE,
    MODE_RENAME,
    MODE_CONFIRM_DELETE,
} explorer_mode_t;

typedef struct {
    char            cwd[FS_PATH_MAX];
    fs_node_t     **entries;
    size_t          entry_count;
    size_t          entry_cap;
    size_t          selection;
    size_t          scroll;
    uint32_t        seen_generation;
    bool            listing_valid;
    explorer_mode_t mode;
    text_input_t    input;
    bool            blink_on;
    uint64_t        blink_at;
    int             hot_button; /* toolbar button under the mouse, or -1 */
    bool            drag_armed; /* pressed on an already-selected row */
    int             drag_start_x, drag_start_y;
} explorer_t;

/* The clipboard is shared by every explorer window, like a real desktop. */
static char clipboard_path[FS_PATH_MAX];
static bool clipboard_cut;

/* Drag-and-drop state, also shared across windows: a drag started in one
 * explorer can be dropped on a folder row in the same window or another. */
static char drag_path[FS_PATH_MAX];
static bool drag_active;
#define DRAG_THRESHOLD 16 /* squared pixels before a press becomes a drag */

typedef struct {
    const char *label;
    int         width;
    int         action;
} toolbar_button_t;

enum {
    BTN_UP = 0,
    BTN_ROOT,
    BTN_HOME,
    BTN_NEWDIR,
    BTN_NEWFILE,
    BTN_RENAME,
    BTN_DELETE,
    BTN_COPY,
    BTN_PASTE,
    BTN_REFRESH,
    BTN_COUNT,
};

static const toolbar_button_t toolbar[BTN_COUNT] = {
    { "Up", 40, BTN_UP },          { "Root", 48, BTN_ROOT },
    { "Home", 52, BTN_HOME },      { "New Dir", 68, BTN_NEWDIR },
    { "New File", 76, BTN_NEWFILE }, { "Rename", 66, BTN_RENAME },
    { "Delete", 62, BTN_DELETE },  { "Copy", 48, BTN_COPY },
    { "Paste", 54, BTN_PASTE },    { "Refresh", 68, BTN_REFRESH },
};

/* ------------------------------------------------------------------ */
/* Listing                                                             */
/* ------------------------------------------------------------------ */

static fs_node_t *explorer_dir(explorer_t *ex)
{
    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), ex->cwd, &node) == FS_OK && node->type == FS_DIR)
        return node;

    /* The directory disappeared: walk up until something exists. */
    char probe[FS_PATH_MAX];
    strlcpy(probe, ex->cwd, sizeof(probe));
    while (strcmp(probe, "/") != 0) {
        char parent[FS_PATH_MAX];
        path_dirname(probe, parent, sizeof(parent));
        strlcpy(probe, parent, sizeof(probe));
        if (fs_resolve(fs_root(), probe, &node) == FS_OK &&
            node->type == FS_DIR) {
            strlcpy(ex->cwd, probe, sizeof(ex->cwd));
            return node;
        }
    }
    strlcpy(ex->cwd, "/", sizeof(ex->cwd));
    return fs_root();
}

static void explorer_rebuild(explorer_t *ex)
{
    fs_node_t *dir = explorer_dir(ex);

    ex->entry_count = 0;
    /* Directories first, then files; both already sorted by name. */
    for (int pass = 0; pass < 2; pass++) {
        fs_type_t want = pass == 0 ? FS_DIR : FS_FILE;
        for (fs_node_t *c = dir->children; c; c = c->next) {
            if (c->type != want)
                continue;
            if (ex->entry_count >= ex->entry_cap) {
                size_t cap = ex->entry_cap ? ex->entry_cap * 2 : 64;
                fs_node_t **grown = krealloc(ex->entries,
                                             cap * sizeof(fs_node_t *));
                if (!grown)
                    break;
                ex->entries   = grown;
                ex->entry_cap = cap;
            }
            ex->entries[ex->entry_count++] = c;
        }
    }

    if (ex->selection >= ex->entry_count)
        ex->selection = ex->entry_count ? ex->entry_count - 1 : 0;
    ex->seen_generation = fs_generation();
    ex->listing_valid   = true;
}

static void explorer_navigate(window_t *win, explorer_t *ex, const char *path)
{
    char resolved[FS_PATH_MAX];
    path_join(ex->cwd, path, resolved, sizeof(resolved));

    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), resolved, &node) != FS_OK ||
        node->type != FS_DIR) {
        desktop_notify("Cannot open %s", resolved);
        return;
    }
    strlcpy(ex->cwd, resolved, sizeof(ex->cwd));
    ex->selection = 0;
    ex->scroll    = 0;
    explorer_rebuild(ex);

    char title[64];
    ksnprintf(title, sizeof(title), "Files - %s", ex->cwd);
    wm_set_title(win, title);
    wm_invalidate(win);
}

static fs_node_t *selected_node(explorer_t *ex)
{
    if (ex->selection >= ex->entry_count)
        return NULL;
    return ex->entries[ex->selection];
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static void begin_prompt(explorer_t *ex, explorer_mode_t mode,
                         const char *initial)
{
    ex->mode = mode;
    text_input_reset(&ex->input, initial);
}

static void action_open(window_t *win, explorer_t *ex)
{
    fs_node_t *node = selected_node(ex);
    if (!node)
        return;
    if (node->type == FS_DIR) {
        explorer_navigate(win, ex, node->name);
        return;
    }
    char path[FS_PATH_MAX];
    fs_path(node, path, sizeof(path));
    app_editor_open(path);
}

static void action_copy(explorer_t *ex, bool cut)
{
    fs_node_t *node = selected_node(ex);
    if (!node) {
        desktop_notify("Nothing selected to copy");
        return;
    }
    fs_path(node, clipboard_path, sizeof(clipboard_path));
    clipboard_cut = cut;
    desktop_notify("%s %s", cut ? "Cut" : "Copied", node->name);
}

/* Build "name (copy)", "name (copy 2)", ... when the name is taken. */
static void unique_name(fs_node_t *dir, const char *base, char *out,
                        size_t size)
{
    if (!fs_child(dir, base)) {
        strlcpy(out, base, size);
        return;
    }
    for (int i = 1; i < 100; i++) {
        if (i == 1)
            ksnprintf(out, size, "%s (copy)", base);
        else
            ksnprintf(out, size, "%s (copy %d)", base, i);
        if (!fs_child(dir, out))
            return;
    }
    strlcpy(out, base, size);
}

static void action_paste(window_t *win, explorer_t *ex)
{
    if (!clipboard_path[0]) {
        desktop_notify("Clipboard is empty");
        return;
    }

    fs_node_t *source = NULL;
    if (fs_resolve(fs_root(), clipboard_path, &source) != FS_OK) {
        desktop_notify("Clipboard item no longer exists");
        clipboard_path[0] = '\0';
        return;
    }

    fs_node_t *dir = explorer_dir(ex);
    if (clipboard_cut) {
        fs_result_t r = fs_move(fs_root(), clipboard_path, ex->cwd);
        if (r != FS_OK) {
            desktop_notify("Move failed: %s", fs_strerror(r));
            return;
        }
        desktop_notify("Moved %s here", source->name);
        clipboard_path[0] = '\0';
    } else {
        char name[FS_NAME_MAX];
        unique_name(dir, source->name, name, sizeof(name));
        fs_result_t r = fs_copy_node(source, dir, name, true, NULL);
        if (r != FS_OK) {
            desktop_notify("Copy failed: %s", fs_strerror(r));
            return;
        }
        desktop_notify("Pasted as %s", name);
    }
    explorer_rebuild(ex);
    wm_invalidate(win);
}

static void action_delete(window_t *win, explorer_t *ex)
{
    fs_node_t *node = selected_node(ex);
    if (!node) {
        desktop_notify("Nothing selected");
        return;
    }
    ex->mode = MODE_CONFIRM_DELETE;
    wm_invalidate(win);
}

static void confirm_delete(window_t *win, explorer_t *ex)
{
    fs_node_t *node = selected_node(ex);
    ex->mode        = MODE_BROWSE;
    if (!node)
        return;

    char name[FS_NAME_MAX];
    strlcpy(name, node->name, sizeof(name));
    fs_result_t r = fs_remove_node(node, true);
    if (r != FS_OK)
        desktop_notify("Delete failed: %s", fs_strerror(r));
    else
        desktop_notify("Deleted %s", name);

    explorer_rebuild(ex);
    wm_invalidate(win);
}

static void commit_prompt(window_t *win, explorer_t *ex)
{
    const char *name = ex->input.buffer;
    fs_node_t  *dir  = explorer_dir(ex);
    fs_result_t r    = FS_OK;

    if (name[0] == '\0') {
        ex->mode = MODE_BROWSE;
        return;
    }

    switch (ex->mode) {
    case MODE_NEW_DIR:
        r = fs_mkdir(dir, name, NULL);
        if (r != FS_OK)
            desktop_notify("Cannot create folder: %s", fs_strerror(r));
        break;
    case MODE_NEW_FILE:
        r = fs_create(dir, name, NULL);
        if (r != FS_OK)
            desktop_notify("Cannot create file: %s", fs_strerror(r));
        break;
    case MODE_RENAME: {
        fs_node_t *node = selected_node(ex);
        if (node) {
            r = fs_rename(node, name);
            if (r != FS_OK)
                desktop_notify("Cannot rename: %s", fs_strerror(r));
        }
        break;
    }
    default:
        break;
    }

    ex->mode = MODE_BROWSE;
    explorer_rebuild(ex);

    /* Select whatever was just created or renamed. */
    for (size_t i = 0; i < ex->entry_count; i++)
        if (strcmp(ex->entries[i]->name, name) == 0)
            ex->selection = i;
    wm_invalidate(win);
}

static void do_toolbar_action(window_t *win, explorer_t *ex, int action)
{
    switch (action) {
    case BTN_UP:
        explorer_navigate(win, ex, "..");
        break;
    case BTN_ROOT:
        explorer_navigate(win, ex, "/");
        break;
    case BTN_HOME:
        explorer_navigate(win, ex, "/home");
        break;
    case BTN_NEWDIR:
        begin_prompt(ex, MODE_NEW_DIR, "");
        break;
    case BTN_NEWFILE:
        begin_prompt(ex, MODE_NEW_FILE, "");
        break;
    case BTN_RENAME: {
        fs_node_t *node = selected_node(ex);
        if (node)
            begin_prompt(ex, MODE_RENAME, node->name);
        else
            desktop_notify("Nothing selected to rename");
        break;
    }
    case BTN_DELETE:
        action_delete(win, ex);
        break;
    case BTN_COPY:
        action_copy(ex, false);
        break;
    case BTN_PASTE:
        action_paste(win, ex);
        break;
    case BTN_REFRESH:
        explorer_rebuild(ex);
        desktop_notify("Refreshed %s", ex->cwd);
        break;
    default:
        break;
    }
    wm_invalidate(win);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static rect_t toolbar_button_rect(rect_t client, int index)
{
    int x = client.x + 4;
    for (int i = 0; i < index; i++)
        x += toolbar[i].width + 3;
    return rect_make(x, client.y + 3, toolbar[index].width, TOOLBAR_H - 6);
}

static size_t visible_rows(rect_t client)
{
    int height = client.h - TOOLBAR_H - PATHBAR_H - STATUS_H;
    return (size_t)MAX(height / ROW_H, 1);
}

static void draw_entry(rect_t row, fs_node_t *node, bool selected, int width)
{
    if (selected) {
        gfx_fill_rect(row.x, row.y, width, row.h, COLOR_SELECTION);
        gfx_rect(row.x, row.y, width, row.h, COLOR_ACCENT);
    }

    widget_icon(row.x + 4, row.y + 2, 16,
                node->type == FS_DIR ? ICON_FOLDER : ICON_FILE);
    gfx_text(row.x + 26, row.y + (row.h - FONT_H) / 2, node->name,
             node->type == FS_DIR ? COLOR_FOLDER : COLOR_TEXT);

    char detail[32];
    if (node->type == FS_DIR)
        ksnprintf(detail, sizeof(detail), "%u items",
                  (uint32_t)fs_child_count(node));
    else
        ksnprintf(detail, sizeof(detail), "%u B", (uint32_t)node->size);
    gfx_text(row.x + width - 100, row.y + (row.h - FONT_H) / 2, detail,
             COLOR_TEXT_DIM);
}

static void explorer_on_draw(window_t *win, rect_t client)
{
    explorer_t *ex = win->priv;
    if (!ex)
        return;
    if (!ex->listing_valid)
        explorer_rebuild(ex);

    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);

    /* Toolbar */
    gfx_fill_rect(client.x, client.y, client.w, TOOLBAR_H, COLOR_PANEL);
    gfx_hline(client.x, client.y + TOOLBAR_H - 1, client.w, COLOR_BORDER);
    for (int i = 0; i < BTN_COUNT; i++) {
        rect_t r = toolbar_button_rect(client, i);
        if (r.x + r.w > client.x + client.w - 4)
            break;
        widget_button(r, toolbar[i].label, ex->hot_button == i, false);
    }

    /* Path bar */
    rect_t path_bar =
        rect_make(client.x, client.y + TOOLBAR_H, client.w, PATHBAR_H);
    gfx_fill_rect(path_bar.x, path_bar.y, path_bar.w, path_bar.h,
                  RGB(0x1c, 0x22, 0x2e));
    gfx_text(path_bar.x + 8, path_bar.y + (PATHBAR_H - FONT_H) / 2, "path:",
             COLOR_TEXT_DIM);
    gfx_clip_push(rect_make(path_bar.x + 52, path_bar.y, path_bar.w - 160,
                            path_bar.h));
    gfx_text(path_bar.x + 52, path_bar.y + (PATHBAR_H - FONT_H) / 2, ex->cwd,
             COLOR_TEXT);
    gfx_clip_pop();

    char summary[48];
    ksnprintf(summary, sizeof(summary), "%u entries",
              (uint32_t)ex->entry_count);
    gfx_text(path_bar.x + path_bar.w - gfx_text_width(summary) - 10,
             path_bar.y + (PATHBAR_H - FONT_H) / 2, summary, COLOR_TEXT_DIM);

    /* Listing */
    int    list_y = client.y + TOOLBAR_H + PATHBAR_H;
    size_t rows   = visible_rows(client);
    int    list_h = (int)rows * ROW_H;
    gfx_fill_rect(client.x, list_y, client.w, list_h, RGB(0x21, 0x27, 0x33));

    if (ex->entry_count == 0) {
        gfx_text(client.x + 26, list_y + 10, "(this folder is empty)",
                 COLOR_TEXT_DIM);
    }
    for (size_t i = 0; i < rows; i++) {
        size_t index = ex->scroll + i;
        if (index >= ex->entry_count)
            break;
        rect_t row = rect_make(client.x, list_y + (int)i * ROW_H, client.w,
                               ROW_H);
        draw_entry(row, ex->entries[index], index == ex->selection,
                   client.w - 8);
    }
    if (ex->entry_count > rows)
        widget_scrollbar(rect_make(client.x + client.w - 6, list_y, 6, list_h),
                         ex->scroll, rows, ex->entry_count);

    /* Status bar or prompt */
    rect_t status = rect_make(client.x, client.y + client.h - STATUS_H,
                              client.w, STATUS_H);
    gfx_fill_rect(status.x, status.y, status.w, status.h, COLOR_PANEL);
    gfx_hline(status.x, status.y, status.w, COLOR_BORDER);

    switch (ex->mode) {
    case MODE_NEW_DIR:
        text_input_draw(&ex->input, status, "New folder:", ex->blink_on);
        break;
    case MODE_NEW_FILE:
        text_input_draw(&ex->input, status, "New file:", ex->blink_on);
        break;
    case MODE_RENAME:
        text_input_draw(&ex->input, status, "Rename to:", ex->blink_on);
        break;
    case MODE_CONFIRM_DELETE: {
        fs_node_t  *node = selected_node(ex);
        char        msg[128];
        ksnprintf(msg, sizeof(msg), "Delete '%s'? Enter = yes, Esc = cancel",
                  node ? node->name : "");
        gfx_fill_rect(status.x, status.y + 1, status.w, status.h - 1,
                      RGB(0x5a, 0x22, 0x22));
        gfx_text(status.x + 8, status.y + (STATUS_H - FONT_H) / 2, msg,
                 COLOR_TEXT);
        break;
    }
    default: {
        const char *hint =
            "Enter open  Bksp up  F2 rename  Del delete  ^C copy  ^V paste"
            "  ^N folder  ^T file  F5 refresh";
        gfx_clip_push(status);
        gfx_text(status.x + 8, status.y + (STATUS_H - FONT_H) / 2, hint,
                 COLOR_TEXT_DIM);
        gfx_clip_pop();
        break;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static void ensure_visible(explorer_t *ex, size_t rows)
{
    if (ex->selection < ex->scroll)
        ex->scroll = ex->selection;
    else if (ex->selection >= ex->scroll + rows)
        ex->scroll = ex->selection - rows + 1;
}

static bool explorer_on_key(window_t *win, const key_event_t *ev)
{
    explorer_t *ex = win->priv;
    if (!ex)
        return false;

    /* Never act on a listing that another window has invalidated. */
    if (fs_generation() != ex->seen_generation)
        explorer_rebuild(ex);

    size_t rows = visible_rows(wm_client_rect(win));

    if (ex->mode == MODE_CONFIRM_DELETE) {
        if (ev->code == KEY_ENTER) {
            confirm_delete(win, ex);
            return true;
        }
        if (ev->code == KEY_ESCAPE) {
            ex->mode = MODE_BROWSE;
            wm_invalidate(win);
            return true;
        }
        return true;
    }

    if (ex->mode != MODE_BROWSE) {
        if (ev->code == KEY_ENTER) {
            commit_prompt(win, ex);
            return true;
        }
        if (ev->code == KEY_ESCAPE) {
            ex->mode = MODE_BROWSE;
            wm_invalidate(win);
            return true;
        }
        if (text_input_key(&ex->input, ev)) {
            ex->blink_on = true;
            wm_invalidate(win);
            return true;
        }
        return true;
    }

    if (ev->mods & KMOD_CTRL) {
        switch (ev->ch) {
        case 'c':
        case 'C':
            action_copy(ex, false);
            wm_invalidate(win);
            return true;
        case 'x':
        case 'X':
            action_copy(ex, true);
            wm_invalidate(win);
            return true;
        case 'v':
        case 'V':
            action_paste(win, ex);
            return true;
        case 'n':
        case 'N':
            begin_prompt(ex, MODE_NEW_DIR, "");
            wm_invalidate(win);
            return true;
        case 't':
        case 'T':
            begin_prompt(ex, MODE_NEW_FILE, "");
            wm_invalidate(win);
            return true;
        default:
            break;
        }
    }

    switch (ev->code) {
    case KEY_UP:
        if (ex->selection > 0)
            ex->selection--;
        break;
    case KEY_DOWN:
        if (ex->selection + 1 < ex->entry_count)
            ex->selection++;
        break;
    case KEY_HOME:
        ex->selection = 0;
        break;
    case KEY_END:
        ex->selection = ex->entry_count ? ex->entry_count - 1 : 0;
        break;
    case KEY_PAGEUP:
        ex->selection = ex->selection > rows ? ex->selection - rows : 0;
        break;
    case KEY_PAGEDOWN:
        ex->selection = MIN(ex->selection + rows,
                            ex->entry_count ? ex->entry_count - 1 : 0);
        break;
    case KEY_ENTER:
        action_open(win, ex);
        return true;
    case KEY_BACKSPACE:
        explorer_navigate(win, ex, "..");
        return true;
    case KEY_DELETE:
        action_delete(win, ex);
        return true;
    case KEY_F2: {
        fs_node_t *node = selected_node(ex);
        if (node)
            begin_prompt(ex, MODE_RENAME, node->name);
        break;
    }
    case KEY_F5:
        explorer_rebuild(ex);
        break;
    default:
        return false;
    }

    ensure_visible(ex, rows);
    wm_invalidate(win);
    return true;
}

static bool explorer_on_mouse(window_t *win, const mouse_event_t *ev, int lx,
                              int ly)
{
    explorer_t *ex = win->priv;
    if (!ex)
        return true;

    rect_t client = wm_client_rect(win);

    if (ev->wheel) {
        /* Negative = wheel pushed away from the user ("scroll up"). */
        size_t rows = visible_rows(client);
        size_t max  = ex->entry_count > rows ? ex->entry_count - rows : 0;
        size_t step = (size_t)(3 * (ev->wheel > 0 ? ev->wheel : -ev->wheel));
        if (ev->wheel < 0)
            ex->scroll = ex->scroll > step ? ex->scroll - step : 0;
        else
            ex->scroll = MIN(ex->scroll + step, max);
        wm_invalidate(win);
        return true;
    }

    /* Toolbar hover and clicks. */
    int hot = -1;
    if (ly < TOOLBAR_H) {
        for (int i = 0; i < BTN_COUNT; i++) {
            rect_t r = toolbar_button_rect(client, i);
            if (rect_contains(r, ev->x, ev->y)) {
                hot = i;
                break;
            }
        }
    }
    if (hot != ex->hot_button) {
        ex->hot_button = hot;
        wm_invalidate(win);
    }
    if (hot >= 0) {
        if (ev->pressed & MOUSE_LEFT)
            do_toolbar_action(win, ex, hot);
        return true;
    }

    int list_top = TOOLBAR_H + PATHBAR_H;
    size_t rows  = visible_rows(client);
    size_t row   = (size_t)((ly - list_top) / ROW_H);
    size_t index = ex->scroll + row;
    bool   in_row = ly >= list_top && row < rows && index < ex->entry_count;

    if (ev->pressed & MOUSE_LEFT) {
        if (in_row && index == ex->selection) {
            /* Pressing an already-selected row could be the start of a
             * drag; whether it turns out to be a click is decided on
             * release, once we know the mouse never moved far enough. */
            ex->drag_armed   = true;
            ex->drag_start_x = ev->x;
            ex->drag_start_y = ev->y;
        } else if (in_row) {
            ex->selection = index;
            wm_invalidate(win);
        }
        return true;
    }

    if ((ev->buttons & MOUSE_LEFT) && ex->drag_armed && !drag_active) {
        int mdx = ev->x - ex->drag_start_x;
        int mdy = ev->y - ex->drag_start_y;
        if (mdx * mdx + mdy * mdy > DRAG_THRESHOLD) {
            fs_node_t *node = selected_node(ex);
            if (node && fs_path(node, drag_path, sizeof(drag_path)))
                drag_active = true;
            else
                ex->drag_armed = false;
        }
        return true;
    }

    if (ev->released & MOUSE_LEFT) {
        if (drag_active) {
            drag_active = false;
            if (in_row) {
                fs_node_t *target = ex->entries[index];
                char       dst[FS_PATH_MAX];
                if (target->type == FS_DIR &&
                    fs_path(target, dst, sizeof(dst)) &&
                    strcmp(dst, drag_path) != 0) {
                    fs_result_t r = fs_move(fs_root(), drag_path, dst);
                    if (r == FS_OK)
                        desktop_notify("Moved into %s", target->name);
                    else
                        desktop_notify("Move failed: %s", fs_strerror(r));
                }
            }
        } else if (ex->drag_armed && in_row && index == ex->selection) {
            action_open(win, ex); /* it was a click, not a drag */
        }
        ex->drag_armed = false;
    }
    return true;
}

static bool explorer_on_tick(window_t *win)
{
    explorer_t *ex = win->priv;
    if (!ex)
        return false;

    bool redraw = false;

    /* Pick up changes made from a terminal or another explorer window. */
    if (fs_generation() != ex->seen_generation) {
        explorer_rebuild(ex);
        redraw = true;
    }

    if (ex->mode != MODE_BROWSE) {
        uint64_t now = timer_ticks();
        if (now >= ex->blink_at) {
            ex->blink_on = !ex->blink_on;
            ex->blink_at = now + 500;
            redraw       = true;
        }
    }
    return redraw;
}

static void explorer_on_open(window_t *win, const char *arg)
{
    explorer_t *ex = kcalloc(1, sizeof(explorer_t));
    if (!ex) {
        klog("[expl] out of memory\n");
        return;
    }
    win->priv      = ex;
    ex->hot_button = -1;
    path_normalize(arg && arg[0] ? arg : "/home", ex->cwd, sizeof(ex->cwd));

    fs_node_t *node = NULL;
    if (fs_resolve(fs_root(), ex->cwd, &node) != FS_OK || node->type != FS_DIR)
        strlcpy(ex->cwd, "/", sizeof(ex->cwd));

    explorer_rebuild(ex);

    char title[64];
    ksnprintf(title, sizeof(title), "Files - %s", ex->cwd);
    wm_set_title(win, title);
}

static void explorer_on_close(window_t *win)
{
    explorer_t *ex = win->priv;
    if (ex)
        kfree(ex->entries);
    kfree(win->priv);
    win->priv = NULL;
}

static const app_class_t explorer_class = {
    .name     = "explorer",
    .on_open  = explorer_on_open,
    .on_close = explorer_on_close,
    .on_draw  = explorer_on_draw,
    .on_key   = explorer_on_key,
    .on_mouse = explorer_on_mouse,
    .on_tick  = explorer_on_tick,
};

window_t *app_explorer_open(const char *path)
{
    return wm_open(&explorer_class, 700, 430, "Files", path);
}
