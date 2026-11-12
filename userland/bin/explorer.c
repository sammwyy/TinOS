#include <tinos/string.h>
#include <tinos/widgets.h>

#define PATH_CAP 256
#define MAX_ENTRIES 64
#define MAX_PREVIEW_LINES 20

enum { ACTION_OPEN = 1, ACTION_UP, ACTION_EDIT, ACTION_DELETE, ACTION_REFRESH };

static tinos_widget_view_t ui;
static unsigned list_index;
static char cwd[PATH_CAP] = "/";
static tinos_dirent_t entries[MAX_ENTRIES];
static char entry_labels[MAX_ENTRIES][68];
static const char *entry_items[MAX_ENTRIES];
static char preview_lines[MAX_PREVIEW_LINES][64];
static const char *preview_items[MAX_PREVIEW_LINES];
static char location[PATH_CAP] = "/";
static char hint[96] = "Enter preview  E edit  Del remove  Backspace parent";
static unsigned count;
static int preview;
static char preview_path[PATH_CAP];

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
    unsigned selected = ui.nodes[list_index].selected;
    count = 0;
    while (count < MAX_ENTRIES &&
           tinos_readdir(cwd, count, &entries[count]) == 1) {
        tinos_strlcpy(entry_labels[count], entries[count].name,
                      sizeof(entry_labels[count]));
        if (entries[count].type == TINOS_DIRENT_DIR) {
            size_t length = strlen(entry_labels[count]);
            if (length + 1 < sizeof(entry_labels[count])) {
                entry_labels[count][length] = '/';
                entry_labels[count][length + 1] = '\0';
            }
        }
        entry_items[count] = entry_labels[count];
        count++;
    }
    tinos_strlcpy(location, cwd, sizeof(location));
    tinos_strlcpy(hint, "Enter preview  E edit  Del remove  Backspace parent",
                  sizeof(hint));
    tinos_widget_list_items(&ui, list_index, entry_items, count,
                            selected < count ? selected : 0);
}

static void render_preview(void)
{
    tinos_strlcpy(location, preview_path, sizeof(location));
    tinos_strlcpy(hint, "Esc or Up: return to directory", sizeof(hint));
    for (unsigned i = 0; i < MAX_PREVIEW_LINES; i++) {
        memset(preview_lines[i], 0, sizeof(preview_lines[i]));
        preview_items[i] = preview_lines[i];
    }
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
    tinos_widget_list_items(&ui, list_index, preview_items, lines, 0);
}

static void enter_selected(void)
{
    unsigned selected = ui.nodes[list_index].selected;
    if (selected >= count)
        return;
    char path[PATH_CAP];
    join_path(entries[selected].name, path);
    if (entries[selected].type == TINOS_DIRENT_DIR) {
        tinos_strlcpy(cwd, path, sizeof(cwd));
        ui.nodes[list_index].selected = 0;
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

static void on_action(tinos_widget_view_t *view, unsigned id, void *context)
{
    (void)view;
    (void)context;
    unsigned selected = ui.nodes[list_index].selected;
    if (id == ACTION_UP) {
        if (preview)
            preview = 0;
        else
            parent();
        ui.nodes[list_index].selected = 0;
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

int main(void)
{
    if (tinos_widget_open(&ui, "File Explorer", 480, 310) < 0)
        return 1;
    ui.on_action = on_action;
    if (tinos_widget_add(&ui, 0, TINOS_WIDGET_BANNER, 0,
                         "FILES / BROWSE", 0, 32, 0) < 0 ||
        tinos_widget_add(&ui, 0, TINOS_WIDGET_LABEL, 0,
                         location, 0, 24, 0) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    int toolbar = tinos_widget_add(&ui, 0, TINOS_WIDGET_ROW, 0, NULL,
                                   0, 42, 0);
    if (toolbar < 0 ||
        tinos_widget_add(&ui, toolbar, TINOS_WIDGET_BUTTON, ACTION_UP,
                         "Up", 64, 28, 1) < 0 ||
        tinos_widget_add(&ui, toolbar, TINOS_WIDGET_BUTTON, ACTION_OPEN,
                         "Open", 72, 28, 1) < 0 ||
        tinos_widget_add(&ui, toolbar, TINOS_WIDGET_BUTTON, ACTION_EDIT,
                         "Edit", 72, 28, 1) < 0 ||
        tinos_widget_add(&ui, toolbar, TINOS_WIDGET_BUTTON, ACTION_DELETE,
                         "Delete", 80, 28, 1) < 0 ||
        tinos_widget_add(&ui, toolbar, TINOS_WIDGET_BUTTON, ACTION_REFRESH,
                         "Reload", 80, 28, 1) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    int list = tinos_widget_list(&ui, 0, ACTION_OPEN, 96, 1);
    if (list < 0 ||
        tinos_widget_add(&ui, 0, TINOS_WIDGET_LABEL, 0,
                         hint, 0, 22, 0) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    list_index = (unsigned)list;
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
                if (event.code == TINOS_KEY_UP ||
                    event.code == TINOS_KEY_DOWN)
                    ui.focused = list;
                tinos_widget_event(&ui, &event);
            }
        } else {
            tinos_widget_event(&ui, &event);
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_widget_close(&ui);
    return 0;
}
