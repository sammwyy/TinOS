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

enum {
    ACTION_OPEN = 1, ACTION_UP, ACTION_EDIT, ACTION_DELETE, ACTION_REFRESH,
    ACTION_BACK, ACTION_FORWARD, ACTION_HOME, ACTION_NEW_DIR,
    ACTION_NEW_FILE, ACTION_RENAME, ACTION_COPY, ACTION_CUT, ACTION_PASTE,
};
enum { MODE_BROWSE, MODE_NEW_DIR, MODE_NEW_FILE, MODE_RENAME,
       MODE_DELETE, MODE_LOCATION };

static tinos_widget_view_t ui;
static char cwd[PATH_CAP] = "/users/sammy";
static tinos_dirent_t entries[MAX_ENTRIES];
static char preview_lines[MAX_PREVIEW_LINES][64];
static unsigned count, selected, scroll, preview_count;
static int preview;
static char preview_path[PATH_CAP];
static tinos_dirent_t home_dirs[8];
static unsigned home_count;
static char history[10][PATH_CAP];
static unsigned history_count, history_at;
static int mode, dragging, drag_moved;
static unsigned drag_source;
static unsigned last_click = MAX_ENTRIES, last_click_ms;
static char form[PATH_CAP], notice[96];
static void refresh(void);

static bool has_suffix(const char *name, const char *suffix)
{
    size_t n = strlen(name), m = strlen(suffix);
    if (n < m)
        return false;
    for (size_t i = 0; i < m; i++) {
        char c = name[n - m + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != suffix[i])
            return false;
    }
    return true;
}

static const char *file_icon(const char *name)
{
    static const struct {
        const char *suffix, *icon;
    } types[] = {
        { ".txt", "text" }, { ".md", "text" }, { ".c", "text" },
        { ".h", "text" }, { ".log", "text" }, { ".conf", "text" },
        { ".bmp", "image_file" }, { ".png", "image_file" },
        { ".jpg", "image_file" }, { ".jpeg", "image_file" },
        { ".webp", "image_file" }, { ".wav", "audio_file" },
        { ".ogg", "audio_file" }, { ".elf", "app" }, { ".so", "chip" },
        { ".app", "settings" },
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        if (has_suffix(name, types[i].suffix))
            return types[i].icon;
    return "file";
}

static const char *const toolbar_icons[] = {
    "back", "forward", "up", "home", "new_folder", "new",
    "rename", "delete", "copy", "paste", "refresh",
};

static void join_path(const char *name, char path[PATH_CAP])
{
    tinos_strlcpy(path, cwd, PATH_CAP);
    unsigned length = strlen(path);
    if (length > 1 && length + 1 < PATH_CAP)
        path[length++] = '/';
    tinos_strlcpy(path + length, name, PATH_CAP - length);
}

static void parent(char path[PATH_CAP])
{
    unsigned length = strlen(path);
    while (length > 1 && path[length - 1] != '/')
        length--;
    path[length > 1 ? length - 1 : 1] = '\0';
}

static void navigate(const char *path, bool remember)
{
    tinos_dirent_t node;
    if (tinos_stat(path, &node) != 1 || node.type != TINOS_DIRENT_DIR) {
        tinos_strlcpy(notice, "Directory not found", sizeof(notice));
        ui.dirty = true;
        return;
    }
    if (remember && strcmp(cwd, path) != 0) {
        if (history_at + 1 < history_count)
            history_count = history_at + 1;
        if (history_count == 10) {
            memmove(history[0], history[1], 9 * PATH_CAP);
            history_count--;
            history_at--;
        }
        tinos_strlcpy(history[history_count++], path, PATH_CAP);
        history_at = history_count - 1;
    }
    tinos_strlcpy(cwd, path, sizeof(cwd));
    selected = scroll = preview = 0;
    notice[0] = '\0';
    refresh();
}

static void refresh(void)
{
    last_click = MAX_ENTRIES;
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
        navigate(path, true);
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
        tinos_canvas_icon(canvas, dir ? "folder" : file_icon(entries[index].name),
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
    if (mode == MODE_DELETE) {
        tinos_canvas_text(canvas, 8, h - STATUS_H + 4, w - 16,
                          "Delete selected item? Enter yes / Esc cancel",
                          theme->danger);
    } else if (mode != MODE_BROWSE) {
        const char *label = mode == MODE_NEW_DIR ? "New folder: " :
                            mode == MODE_NEW_FILE ? "New file: " :
                            mode == MODE_RENAME ? "Rename: " : "Go to: ";
        tinos_canvas_text(canvas, 8, h - STATUS_H + 4, 100, label,
                          theme->foreground);
        tinos_canvas_text(canvas, 8 + (int)strlen(label) * 8,
                          h - STATUS_H + 4, w - 22 - (int)strlen(label) * 8,
                          form, theme->foreground);
    } else {
        tinos_canvas_text(canvas, 8, h - STATUS_H + 4, w - 16,
                          notice[0] ? notice :
                          preview ? "Esc or Up: return to directory" :
                          "Enter open  Bksp up  Ctrl+N new  F2 rename",
                          notice[0] ? theme->accent : theme->muted);
    }
}

static void begin_prompt(int next_mode, const char *initial)
{
    mode = next_mode;
    tinos_strlcpy(form, initial ? initial : "", sizeof(form));
    ui.dirty = true;
}

static void set_notice(const char *text)
{
    tinos_strlcpy(notice, text, sizeof(notice));
    ui.dirty = true;
}

static void finish_prompt(void)
{
    char path[PATH_CAP];
    int action = mode;
    mode = MODE_BROWSE;
    if (action == MODE_LOCATION) {
        navigate(form, true);
        return;
    }
    if (action == MODE_DELETE) {
        if (selected < count) {
            join_path(entries[selected].name, path);
            if (tinos_unlink(path) < 0)
                set_notice("Cannot delete (directory may not be empty)");
            else {
                refresh();
                set_notice("Deleted");
            }
        }
        return;
    }
    if (!form[0] || strlen(form) >= sizeof(entries[0].name) ||
        strcmp(form, ".") == 0 || strcmp(form, "..") == 0) {
        set_notice("Invalid name");
        return;
    }
    for (const char *at = form; *at; at++)
        if (*at == '/') {
            set_notice("A name cannot contain /");
            return;
        }
    if (strlen(cwd) + strlen(form) + 2 > PATH_CAP) {
        set_notice("Path is too long");
        return;
    }
    join_path(form, path);
    int result = -1;
    if (action == MODE_NEW_DIR)
        result = tinos_mkdir(path);
    else if (action == MODE_NEW_FILE) {
        tinos_dirent_t existing;
        if (tinos_stat(path, &existing) != 1) {
            int fd = tinos_open_flags(path, TINOS_O_CREAT | TINOS_O_WRONLY);
            if (fd >= 0) {
                tinos_close(fd);
                result = 0;
            }
        }
    } else if (action == MODE_RENAME && selected < count) {
        char source[PATH_CAP];
        join_path(entries[selected].name, source);
        result = strcmp(path, source) == 0 ? 0 :
                 tinos_rename(source, path);
    }
    if (result == 0) {
        refresh();
        set_notice("Done");
    } else {
        set_notice("Operation failed (name may already exist)");
    }
}

static void copy_selection(bool move)
{
    if (selected >= count)
        return;
    char path[PATH_CAP];
    join_path(entries[selected].name, path);
    if (tinos_clipboard_set(path, strlen(path) + 1,
            move ? TINOS_CLIPBOARD_FILE_MOVE :
                   TINOS_CLIPBOARD_FILE_COPY) == 0)
        set_notice(move ? "Cut: choose destination and paste" :
                          "Copied: choose destination and paste");
    else
        set_notice("Clipboard unavailable");
}

static void paste(void)
{
    char source[PATH_CAP];
    unsigned kind;
    int size = tinos_clipboard_get(source, sizeof(source), &kind);
    if (size < 2 || size > PATH_CAP || source[size - 1] != '\0' ||
        strlen(source) != (unsigned)size - 1 || source[0] != '/' ||
        (kind != TINOS_CLIPBOARD_FILE_COPY &&
         kind != TINOS_CLIPBOARD_FILE_MOVE)) {
        set_notice("No file in clipboard");
        return;
    }
    const char *name = source;
    for (const char *at = source; *at; at++)
        if (*at == '/')
            name = at + 1;
    char target[PATH_CAP];
    if (strlen(cwd) + strlen(name) + 2 > PATH_CAP) {
        set_notice("Path is too long");
        return;
    }
    join_path(name, target);
    if (strcmp(source, target) == 0) {
        if (kind == TINOS_CLIPBOARD_FILE_MOVE) {
            set_notice("Already in this directory");
            return;
        }
        size_t len = strlen(target);
        if (len + 5 >= sizeof(target)) {
            set_notice("Path is too long");
            return;
        }
        tinos_strlcpy(target + len, "-copy", sizeof(target) - len);
    }
    int result = kind == TINOS_CLIPBOARD_FILE_COPY ?
                 tinos_copy(source, target) : tinos_rename(source, target);
    if (result < 0)
        set_notice("Paste failed (already exists or exceeds memory limit)");
    else {
        refresh();
        set_notice(kind == TINOS_CLIPBOARD_FILE_MOVE ? "Moved" : "Copied");
    }
}

static void on_action(tinos_widget_view_t *view, unsigned id, void *context)
{
    (void)view;
    (void)context;
    if (id == ACTION_BACK || id == ACTION_FORWARD) {
        if (id == ACTION_BACK && history_at)
            history_at--;
        else if (id == ACTION_FORWARD && history_at + 1 < history_count)
            history_at++;
        else
            return;
        navigate(history[history_at], false);
    } else if (id == ACTION_HOME) {
        navigate("/users/sammy", true);
    } else if (id == ACTION_UP) {
        if (preview)
            preview = 0;
        else {
            char path[PATH_CAP];
            tinos_strlcpy(path, cwd, sizeof(path));
            parent(path);
            navigate(path, true);
        }
        selected = scroll = 0;
        refresh();
    } else if (id == ACTION_REFRESH) {
        if (preview)
            render_preview();
        else
            refresh();
    } else if (id == ACTION_PASTE) {
        paste();
    } else if (preview) {
        return;
    } else if (id == ACTION_NEW_DIR || id == ACTION_NEW_FILE) {
        begin_prompt(id == ACTION_NEW_DIR ? MODE_NEW_DIR : MODE_NEW_FILE, "");
    } else if (id == ACTION_COPY || id == ACTION_CUT) {
        copy_selection(id == ACTION_CUT);
    } else if (id == ACTION_OPEN) {
        enter_selected();
    } else if (selected < count) {
        char path[PATH_CAP];
        join_path(entries[selected].name, path);
        if (id == ACTION_DELETE) {
            begin_prompt(MODE_DELETE, "");
        } else if (id == ACTION_RENAME) {
            begin_prompt(MODE_RENAME, entries[selected].name);
        } else if (id == ACTION_EDIT &&
                   entries[selected].type == TINOS_DIRENT_FILE) {
            const char *args[] = { "/apps/gui/editor.elf", path };
            if (tinos_spawn(args[0], 2, args) < 0)
                set_notice("Unable to launch editor (low memory)");
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
    history_count = 1;
    tinos_strlcpy(history[0], cwd, PATH_CAP);
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
        if (event.type == TINOS_GUI_MOUSE && (int32_t)event.ch) {
            int step = (int32_t)event.ch > 0 ? -3 : 3;
            int max = (int)(preview ? preview_count : count) -
                      (int)visible_rows();
            int next = (int)scroll + step;
            if (next < 0) next = 0;
            if (next > max) next = max > 0 ? max : 0;
            scroll = (unsigned)next;
            ui.dirty = true;
        }
        if (event.type == TINOS_GUI_MOUSE && mode == MODE_BROWSE &&
            (event.code & TINOS_GUI_MOUSE_LEFT)) {
            int sidebar = ui.width > 360 ? SIDEBAR_W : ui.width / 3;
            if (event.y >= 3 && event.y < TOOLBAR_H - 3 &&
                event.x >= 4 && event.x < 4 + 11 * 29) {
                int button = (event.x - 4) / 29;
                static const unsigned actions[] = {
                    ACTION_BACK, ACTION_FORWARD, ACTION_UP, ACTION_HOME,
                    ACTION_NEW_DIR, ACTION_NEW_FILE, ACTION_RENAME,
                    ACTION_DELETE, ACTION_COPY, ACTION_PASTE, ACTION_REFRESH,
                };
                on_action(&ui, actions[button], NULL);
            } else if (event.y >= TOOLBAR_H &&
                       event.y < TOOLBAR_H + PATHBAR_H &&
                       event.x >= 76) {
                begin_prompt(MODE_LOCATION, cwd);
            } else if (event.y >= TOOLBAR_H + PATHBAR_H &&
                       event.y < (int)ui.height - STATUS_H) {
                if (event.x < sidebar) {
                    int tree_y = TOOLBAR_H + PATHBAR_H + 28;
                    if (event.y < tree_y) {
                        navigate("/users/sammy", true);
                    } else if (event.y < tree_y + (int)home_count * ROW_H) {
                        unsigned folder = (event.y - tree_y) / ROW_H;
                        char destination[PATH_CAP] = "/users/sammy/";
                        tinos_strlcpy(destination + strlen(destination),
                                      home_dirs[folder].name,
                                      sizeof(destination) - strlen(destination));
                        navigate(destination, true);
                    } else if (event.y >= tree_y + (int)home_count * ROW_H + 40) {
                        navigate("/drive/disk0", true);
                    }
                } else {
                    unsigned row = (event.y - TOOLBAR_H - PATHBAR_H) / ROW_H;
                    unsigned index = scroll + row;
                    if (!preview && index < count) {
                        tinos_system_info_t clock;
                        unsigned now = tinos_sysinfo(&clock) == 0 ?
                                       clock.uptime_ms : 0;
                        bool double_click = last_click == index && now &&
                            now - last_click_ms < 350;
                        selected = index;
                        last_click = index;
                        last_click_ms = now;
                        if (double_click) {
                            dragging = 0;
                            enter_selected();
                        } else {
                            dragging = 1;
                            drag_source = index;
                            drag_moved = 0;
                            ui.dirty = true;
                        }
                    }
                }
            }
        }
        if (event.type == TINOS_GUI_MOUSE && dragging &&
            (event.mods & TINOS_GUI_MOUSE_LEFT) && !event.code)
            drag_moved = 1;
        if (event.type == TINOS_GUI_MOUSE && dragging &&
            (event.mods >> 8 & TINOS_GUI_MOUSE_LEFT)) {
            dragging = 0;
            int sidebar = ui.width > 360 ? SIDEBAR_W : ui.width / 3;
            if (drag_moved && !preview && drag_source < count &&
                event.x >= sidebar &&
                event.y >= TOOLBAR_H + PATHBAR_H &&
                event.y < (int)ui.height - STATUS_H) {
                unsigned target = scroll +
                    (event.y - TOOLBAR_H - PATHBAR_H) / ROW_H;
                if (target < count && target != drag_source &&
                    entries[target].type == TINOS_DIRENT_DIR) {
                    char source[PATH_CAP], destination[PATH_CAP];
                    join_path(entries[drag_source].name, source);
                    join_path(entries[target].name, destination);
                    size_t len = strlen(destination);
                    if (len + 1 + strlen(entries[drag_source].name) <
                        sizeof(destination)) {
                        destination[len++] = '/';
                        tinos_strlcpy(destination + len,
                                      entries[drag_source].name,
                                      sizeof(destination) - len);
                        if (tinos_rename(source, destination) == 0) {
                            refresh();
                            set_notice("Moved into folder");
                        } else
                            set_notice("Move failed (destination exists)");
                    }
                }
            }
        }
        if (event.type == TINOS_GUI_KEY) {
            if (mode != MODE_BROWSE) {
                if (event.code == TINOS_KEY_ESCAPE) {
                    mode = MODE_BROWSE;
                    ui.dirty = true;
                } else if (event.code == TINOS_KEY_ENTER) {
                    finish_prompt();
                } else if (mode != MODE_DELETE) {
                    size_t length = strlen(form);
                    if ((event.mods & TINOS_MOD_CTRL) &&
                        (event.ch == 'a' || event.ch == 'A'))
                        form[0] = '\0';
                    else if (event.code == TINOS_KEY_BACKSPACE && length)
                        form[length - 1] = '\0';
                    else if (event.ch >= 32 && event.ch < 127 &&
                             length + 1 < sizeof(form)) {
                        form[length] = (char)event.ch;
                        form[length + 1] = '\0';
                    }
                    ui.dirty = true;
                }
                if (tinos_widget_flush(&ui) < 0)
                    break;
                continue;
            }
            if (event.mods & TINOS_MOD_CTRL) {
                if (event.ch == 'c' || event.ch == 'C')
                    on_action(&ui, ACTION_COPY, NULL);
                else if (event.ch == 'x' || event.ch == 'X')
                    on_action(&ui, ACTION_CUT, NULL);
                else if (event.ch == 'v' || event.ch == 'V')
                    on_action(&ui, ACTION_PASTE, NULL);
                else if (event.ch == 'n' || event.ch == 'N')
                    on_action(&ui, ACTION_NEW_DIR, NULL);
                else if (event.ch == 't' || event.ch == 'T')
                    on_action(&ui, ACTION_NEW_FILE, NULL);
                ui.dirty = true;
                if (tinos_widget_flush(&ui) < 0)
                    break;
                continue;
            }
            if (event.code == TINOS_KEY_ESCAPE) {
                if (!preview)
                    break;
                on_action(&ui, ACTION_UP, NULL);
            } else if (event.code == TINOS_KEY_BACKSPACE) {
                on_action(&ui, ACTION_UP, NULL);
            } else if (!preview && event.code == TINOS_KEY_DELETE) {
                on_action(&ui, ACTION_DELETE, NULL);
            } else if (!preview && event.code == TINOS_KEY_F2) {
                on_action(&ui, ACTION_RENAME, NULL);
            } else if (event.code == TINOS_KEY_F5) {
                on_action(&ui, ACTION_REFRESH, NULL);
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
                else if (!preview && event.code == TINOS_KEY_PAGEUP)
                    selected = selected > visible_rows() ?
                               selected - visible_rows() : 0;
                else if (!preview && event.code == TINOS_KEY_PAGEDOWN)
                    selected = count ?
                        (selected + visible_rows() < count ?
                         selected + visible_rows() : count - 1) : 0;
                else if (!preview && event.code == TINOS_KEY_HOME)
                    selected = 0;
                else if (!preview && event.code == TINOS_KEY_END && count)
                    selected = count - 1;
                else if (!preview && event.code == TINOS_KEY_ENTER)
                    enter_selected();
                if (selected < scroll)
                    scroll = selected;
                else if (selected >= scroll + visible_rows())
                    scroll = selected - visible_rows() + 1;
                ui.dirty = true;
            }
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
