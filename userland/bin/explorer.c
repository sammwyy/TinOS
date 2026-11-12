#include <tinos/string.h>
#include <tinos/widgets.h>

#define PATH_CAP 256
#define MAX_ENTRIES 64
#define MAX_PREVIEW_LINES 20
#define TOOLBAR_H 28
#define PATHBAR_H 24
#define STATUS_H 22
#define SIDEBAR_W 148
#define ROW_H 20

enum { ACTION_OPEN = 1, ACTION_UP, ACTION_EDIT, ACTION_DELETE, ACTION_REFRESH };

static tinos_widget_view_t ui;
static char cwd[PATH_CAP] = "/users/sammy";
static tinos_dirent_t entries[MAX_ENTRIES];
static char preview_lines[MAX_PREVIEW_LINES][64];
static unsigned count, selected, scroll, preview_count;
static int preview;
static char preview_path[PATH_CAP];
static tinos_dirent_t home_dirs[8];
static unsigned home_count;

static const char *const toolbar_icons[] = {
    "back", "forward", "up", "home", "folder", "new",
    "editor", "power", "file", "file", "refresh",
};

static void join_path(const char *name, char path[PATH_CAP])
{
    tinos_strlcpy(path, cwd, PATH_CAP);
    unsigned length = strlen(path);
    if (length > 1 && length + 1 < PATH_CAP)
        path[length++] = '/';
    tinos_strlcpy(path + length, name, PATH_CAP - length);
}

static void parent(void)
{
    unsigned length = strlen(cwd);
    while (length > 1 && cwd[length - 1] != '/')
        length--;
    cwd[length > 1 ? length - 1 : 1] = '\0';
}

static void refresh(void)
{
    count = 0;
    for (unsigned index = 0; index < 128 && count < MAX_ENTRIES; index++) {
        tinos_dirent_t entry;
        if (tinos_readdir(cwd, index, &entry) != 1)
            break;
        if (entry.name[0] != '.')
            entries[count++] = entry;
    }
    if (selected >= count)
        selected = count ? count - 1 : 0;
    home_count = 0;
    for (unsigned i = 0; i < 8; i++) {
        tinos_dirent_t entry;
        if (tinos_readdir("/users/sammy", i, &entry) != 1)
            break;
        if (entry.type == TINOS_DIRENT_DIR && entry.name[0] != '.')
            home_dirs[home_count++] = entry;
    }
    ui.dirty = true;
}

static void render_preview(void)
{
    for (unsigned i = 0; i < MAX_PREVIEW_LINES; i++)
        memset(preview_lines[i], 0, sizeof(preview_lines[i]));
    unsigned lines = 1;
    int fd = tinos_open(preview_path);
    if (fd < 0) {
        tinos_strlcpy(preview_lines[0], "Unable to read file.",
                      sizeof(preview_lines[0]));
    } else {
        char buffer[512];
        int length = tinos_read(fd, buffer, sizeof(buffer));
        tinos_close(fd);
        if (length > 0) {
            unsigned x = 0, y = 0;
            for (int i = 0; i < length; i++) {
                if (buffer[i] == '\n' || x + 1 >= sizeof(preview_lines[0])) {
                    x = 0;
                    if (++y == MAX_PREVIEW_LINES)
                        break;
                    lines = y + 1;
                }
                if (buffer[i] == '\n')
                    continue;
                if (y >= MAX_PREVIEW_LINES)
                    break;
                if (buffer[i] >= 32 && buffer[i] < 127)
                    preview_lines[y][x++] = buffer[i];
            }
        }
    }
    preview_count = lines;
    scroll = 0;
    ui.dirty = true;
}

static void enter_selected(void)
{
    if (selected >= count)
        return;
    char path[PATH_CAP];
    join_path(entries[selected].name, path);
    if (entries[selected].type == TINOS_DIRENT_DIR) {
        tinos_strlcpy(cwd, path, sizeof(cwd));
        selected = scroll = 0;
        refresh();
    } else {
        size_t length = strlen(path);
        if (length >= 4 && strcmp(path + length - 4, ".bmp") == 0) {
            const char *args[] = { "/apps/gui/image_viewer.elf", path };
            tinos_spawn(args[0], 2, args);
            return;
        }
        if (length >= 4 && strcmp(path + length - 4, ".wav") == 0) {
            const char *args[] = { "/apps/gui/media_player.elf", path };
            tinos_spawn(args[0], 2, args);
            return;
        }
        tinos_strlcpy(preview_path, path, sizeof(preview_path));
        preview = 1;
        render_preview();
    }
}

static unsigned visible_rows(void)
{
    int usable = (int)ui.height - TOOLBAR_H - PATHBAR_H - STATUS_H;
    return (unsigned)(usable > ROW_H ? usable / ROW_H : 1);
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    int list_y = TOOLBAR_H + PATHBAR_H;
    int list_h = h - list_y - STATUS_H;
    int sidebar = w > 360 ? SIDEBAR_W : w / 3;
    int list_w = w - sidebar;
    tinos_canvas_fill(canvas, 0, 0, w, h, theme->background);

    tinos_canvas_fill(canvas, 0, 0, w, TOOLBAR_H, theme->panel);
    tinos_canvas_fill(canvas, 0, TOOLBAR_H - 1, w, 1, theme->border);
    for (unsigned i = 0; i < sizeof(toolbar_icons) / sizeof(toolbar_icons[0]); i++) {
        int x = 4 + (int)i * 29;
        if (x + 26 > w - 4)
            break;
        tinos_canvas_button(canvas, x, 3, 26, 22, NULL, false);
        tinos_canvas_icon(canvas, toolbar_icons[i], x + 5, 6, 16);
        if (i == 3)
            tinos_canvas_fill(canvas, x + 29, 6, 1, 16, theme->border);
    }
    tinos_canvas_fill(canvas, 0, TOOLBAR_H, w, PATHBAR_H, theme->panel);
    tinos_canvas_text(canvas, 8, TOOLBAR_H + 4, 72, "Location:",
                      theme->muted);
    int path_w = w > 490 ? 286 : w - 160;
    tinos_canvas_fill(canvas, 76, TOOLBAR_H + 3, path_w, 18, 0xf4f3f6u);
    tinos_canvas_frame(canvas, 76, TOOLBAR_H + 3, path_w, 18,
                       theme->border);
    tinos_canvas_text(canvas, 82, TOOLBAR_H + 4, path_w - 12,
                      preview ? preview_path : cwd, theme->foreground);
    char summary[28];
    tinos_u32(summary, preview ? preview_count : count);
    tinos_strlcpy(summary + strlen(summary), " entries",
                  sizeof(summary) - strlen(summary));
    tinos_canvas_text(canvas, 90 + path_w, TOOLBAR_H + 4,
                      w - path_w - 94, summary, theme->muted);

    tinos_canvas_fill(canvas, 0, list_y, sidebar, list_h, 0xe1e0e7u);
    tinos_canvas_fill(canvas, sidebar, list_y, list_w, list_h, 0xeeeff1u);
    tinos_canvas_fill(canvas, sidebar, list_y, 1, list_h, theme->border);
    tinos_canvas_icon(canvas, "home", 8, list_y + 4, 16);
    tinos_canvas_text(canvas, 30, list_y + 8, sidebar - 32,
                      "Home Folder", theme->foreground);
    int tree_y = list_y + 28;
    for (unsigned i = 0; i < home_count && tree_y + ROW_H < h - STATUS_H - 50;
         i++, tree_y += ROW_H) {
        tinos_canvas_icon(canvas, "folder", 28, tree_y, 16);
        tinos_canvas_text(canvas, 48, tree_y, sidebar - 50,
                          home_dirs[i].name, theme->muted);
    }
    int drive_y = tree_y + 10;
    tinos_canvas_icon(canvas, "drive", 8, drive_y + 6, 16);
    tinos_canvas_text(canvas, 30, drive_y + 10, sidebar - 32,
                      "Drives", theme->foreground);
    tinos_canvas_icon(canvas, "drive", 28, drive_y + 30, 16);
    tinos_canvas_text(canvas, 48, drive_y + 30, sidebar - 50,
                      "Disk 0", theme->muted);

    unsigned rows = visible_rows();
    unsigned total = preview ? preview_count : count;
    if (!preview) {
        if (selected < scroll)
            scroll = selected;
        if (selected >= scroll + rows)
            scroll = selected - rows + 1;
    }
    for (unsigned row = 0; row < rows && row + scroll < total; row++) {
        unsigned index = row + scroll;
        int y = list_y + (int)row * ROW_H;
        if (y + ROW_H > h - STATUS_H)
            break;
        if (!preview && index == selected)
            tinos_canvas_fill(canvas, sidebar + 1, y, list_w - 2,
                              ROW_H, theme->selection);
        else
            tinos_canvas_fill(canvas, sidebar + 1, y + ROW_H - 1,
                              list_w - 2, 1, 0xdcdfebu);
        if (preview) {
            tinos_canvas_text(canvas, sidebar + 12, y + 2, list_w - 20,
                              preview_lines[index], theme->foreground);
            continue;
        }
        bool dir = entries[index].type == TINOS_DIRENT_DIR;
        tinos_canvas_icon(canvas, dir ? "folder" : "file",
                          sidebar + 4, y + 2, 16);
        tinos_canvas_text(canvas, sidebar + 26, y + 2, list_w - 142,
                          entries[index].name,
                          index == selected ? 0xffffffu :
                          (dir ? 0x8b5900u : theme->foreground));
        char detail[24];
        if (dir)
            tinos_strlcpy(detail, "folder", sizeof(detail));
        else {
            tinos_u32(detail, entries[index].size);
            tinos_strlcpy(detail + strlen(detail), " B",
                          sizeof(detail) - strlen(detail));
        }
        if (list_w >= 280)
            tinos_canvas_text(canvas, w - 99, y + 2, 96, detail,
                              index == selected ? 0xe5edf6u : theme->muted);
    }
    if (list_w >= 280)
        tinos_canvas_fill(canvas, w - 108, list_y, 1, list_h, 0xc2c7cfu);
    tinos_canvas_fill(canvas, 0, h - STATUS_H, w, STATUS_H, theme->panel);
    tinos_canvas_fill(canvas, 0, h - STATUS_H, w, 1, theme->border);
    tinos_canvas_text(canvas, 8, h - STATUS_H + 4, w - 16,
                      preview ? "Esc or Up: return to directory" :
                      "Enter open  Bksp up  E edit  Del delete  F5 refresh",
                      theme->muted);
}

static void on_action(tinos_widget_view_t *view, unsigned id, void *context)
{
    (void)view;
    (void)context;
    if (id == ACTION_UP) {
        if (preview)
            preview = 0;
        else
            parent();
        selected = scroll = 0;
        refresh();
    } else if (id == ACTION_REFRESH) {
        if (preview)
            render_preview();
        else
            refresh();
    } else if (preview) {
        return;
    } else if (id == ACTION_OPEN) {
        enter_selected();
    } else if (selected < count) {
        char path[PATH_CAP];
        join_path(entries[selected].name, path);
        if (id == ACTION_DELETE) {
            tinos_unlink(path);
            refresh();
        } else if (id == ACTION_EDIT &&
                   entries[selected].type == TINOS_DIRENT_FILE) {
            const char *args[] = { "/apps/gui/editor.elf", path };
            tinos_spawn(args[0], 2, args);
        }
    }
}

int main(int argc, char **argv)
{
    tinos_dirent_t directory;
    if (argc > 1 && argv[1][0] == '/' &&
        tinos_stat(argv[1], &directory) == 1 &&
        directory.type == TINOS_DIRENT_DIR)
        tinos_strlcpy(cwd, argv[1], sizeof(cwd));
    else if (tinos_stat(cwd, &directory) != 1 ||
             directory.type != TINOS_DIRENT_DIR)
        tinos_strlcpy(cwd, "/", sizeof(cwd));
    tinos_system_info_t info;
    bool full_size = tinos_sysinfo(&info) == 0 &&
                     info.memory_total_kib >= 12288 &&
                     info.screen_width >= 760;
    if (tinos_widget_open(&ui, "File Explorer",
                          full_size ? 700 : 480,
                          full_size ? 430 : 310) < 0)
        return 1;
    ui.on_action = on_action;
    ui.on_paint = paint;
    refresh();
    if (tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE)
            break;
        if (event.type == TINOS_GUI_CHILD_EXIT) {
            tinos_wait(event.code, 0);
            if (!preview)
                refresh();
        }
        if (event.type == TINOS_GUI_MOUSE && event.code == 1) {
            int sidebar = ui.width > 360 ? SIDEBAR_W : ui.width / 3;
            if (event.y >= 3 && event.y < TOOLBAR_H - 3 &&
                event.x >= 4 && event.x < 4 + 11 * 29) {
                int button = (event.x - 4) / 29;
                if (button == 2)
                    on_action(&ui, ACTION_UP, NULL);
                else if (button == 3) {
                    tinos_strlcpy(cwd, "/users/sammy", sizeof(cwd));
                    selected = scroll = preview = 0;
                    refresh();
                } else if (button == 6)
                    on_action(&ui, ACTION_EDIT, NULL);
                else if (button == 7)
                    on_action(&ui, ACTION_DELETE, NULL);
                else if (button == 10)
                    on_action(&ui, ACTION_REFRESH, NULL);
            } else if (event.y >= TOOLBAR_H + PATHBAR_H &&
                       event.y < (int)ui.height - STATUS_H) {
                if (event.x < sidebar) {
                    int tree_y = TOOLBAR_H + PATHBAR_H + 28;
                    if (event.y < tree_y) {
                        tinos_strlcpy(cwd, "/users/sammy", sizeof(cwd));
                        selected = scroll = preview = 0;
                        refresh();
                    } else if (event.y < tree_y + (int)home_count * ROW_H) {
                        unsigned folder = (event.y - tree_y) / ROW_H;
                        tinos_strlcpy(cwd, "/users/sammy/", sizeof(cwd));
                        tinos_strlcpy(cwd + strlen(cwd),
                                      home_dirs[folder].name,
                                      sizeof(cwd) - strlen(cwd));
                        selected = scroll = preview = 0;
                        refresh();
                    } else if (event.y >= tree_y + (int)home_count * ROW_H + 40) {
                        tinos_strlcpy(cwd, "/drive/disk0", sizeof(cwd));
                        selected = scroll = preview = 0;
                        refresh();
                    }
                } else {
                    unsigned row = (event.y - TOOLBAR_H - PATHBAR_H) / ROW_H;
                    unsigned index = scroll + row;
                    if (!preview && index < count) {
                        selected = index;
                        ui.dirty = true;
                    }
                }
            }
        }
        if (event.type == TINOS_GUI_KEY) {
            if (event.code == TINOS_KEY_ESCAPE) {
                if (!preview)
                    break;
                on_action(&ui, ACTION_UP, NULL);
            } else if (event.code == TINOS_KEY_BACKSPACE) {
                on_action(&ui, ACTION_UP, NULL);
            } else if (!preview && event.code == TINOS_KEY_DELETE) {
                on_action(&ui, ACTION_DELETE, NULL);
            } else if (!preview && (event.ch == 'e' || event.ch == 'E')) {
                on_action(&ui, ACTION_EDIT, NULL);
            } else if (event.ch == 'r' || event.ch == 'R') {
                on_action(&ui, ACTION_REFRESH, NULL);
            } else {
                if (!preview && event.code == TINOS_KEY_UP && selected)
                    selected--;
                else if (!preview && event.code == TINOS_KEY_DOWN &&
                         selected + 1 < count)
                    selected++;
                else if (!preview && event.code == TINOS_KEY_ENTER)
                    enter_selected();
                ui.dirty = true;
            }
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
