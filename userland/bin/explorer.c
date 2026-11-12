#include <tinos/string.h>
#include <tinos/ui.h>

#define PATH_CAP 256
#define MAX_ENTRIES 64

static tinos_ui_t ui;
static char cwd[PATH_CAP] = "/";
static tinos_dirent_t entries[MAX_ENTRIES];
static unsigned count, selected, first_visible;
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
    count = 0;
    while (count < MAX_ENTRIES &&
           tinos_readdir(cwd, count, &entries[count]) == 1)
        count++;
    if (selected >= count)
        selected = count ? count - 1 : 0;
}

static void render_preview(void)
{
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 0, 0, preview_path);
    tinos_ui_text(&ui, 0, 1, "Escape: directory");
    int fd = tinos_open(preview_path);
    if (fd < 0) {
        tinos_ui_text(&ui, 0, 3, "Unable to read file.");
    } else {
        char buffer[512];
        int length = tinos_read(fd, buffer, sizeof(buffer));
        tinos_close(fd);
        if (length > 0) {
            unsigned x = 0, y = 3;
            for (int i = 0; i < length && y < ui.rows; i++) {
                if (buffer[i] == '\n' || x >= ui.columns) {
                    x = 0;
                    y++;
                }
                if (y >= ui.rows)
                    break;
                if (buffer[i] >= 32 && buffer[i] < 127)
                    ui.cells[y * ui.columns + x++] = buffer[i];
            }
        }
    }
    tinos_ui_present(&ui);
}

static void render(void)
{
    if (preview) {
        render_preview();
        return;
    }
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 0, 0, "File Explorer");
    tinos_ui_text(&ui, 0, 1, cwd);
    tinos_ui_text(&ui, 0, 2, "Enter preview  E edit  Del remove  Backspace parent");
    unsigned visible = ui.rows > 4 ? ui.rows - 4 : 1;
    if (selected < first_visible)
        first_visible = selected;
    if (selected >= first_visible + visible)
        first_visible = selected - visible + 1;
    for (unsigned i = first_visible; i < count && i < first_visible + visible; i++) {
        unsigned row = 3 + i - first_visible;
        tinos_ui_text(&ui, 0, row, i == selected ? "> " : "  ");
        tinos_ui_text(&ui, 2, row, entries[i].name);
        if (entries[i].type == TINOS_DIRENT_DIR)
            tinos_ui_text(&ui, 2 + strlen(entries[i].name), row, "/");
    }
    tinos_ui_present(&ui);
}

static void enter_selected(void)
{
    if (selected >= count)
        return;
    char path[PATH_CAP];
    join_path(entries[selected].name, path);
    if (entries[selected].type == TINOS_DIRENT_DIR) {
        tinos_strlcpy(cwd, path, sizeof(cwd));
        selected = first_visible = 0;
        refresh();
    } else {
        tinos_strlcpy(preview_path, path, sizeof(preview_path));
        preview = 1;
    }
    render();
}

int main(void)
{
    if (tinos_ui_open(&ui, "File Explorer", 500, 320) < 0)
        return 1;
    refresh();
    render();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE)
            break;
        if (event.type == TINOS_GUI_MOUSE && !preview &&
            event.y >= 3 * 16 && event.code == 1) {
            unsigned row = event.y / 16 - 3 + first_visible;
            if (row < count) {
                selected = row;
                render();
            }
        }
        if (event.type != TINOS_GUI_KEY)
            continue;
        if (preview) {
            if (event.code == TINOS_KEY_ESCAPE ||
                event.code == TINOS_KEY_BACKSPACE) {
                preview = 0;
                render();
            }
            continue;
        }
        switch (event.code) {
        case TINOS_KEY_UP:
            if (selected)
                selected--;
            break;
        case TINOS_KEY_DOWN:
            if (selected + 1 < count)
                selected++;
            break;
        case TINOS_KEY_ENTER:
            enter_selected();
            continue;
        case TINOS_KEY_DELETE:
            if (selected < count) {
                char path[PATH_CAP];
                join_path(entries[selected].name, path);
                tinos_unlink(path);
                refresh();
            }
            break;
        case TINOS_KEY_BACKSPACE:
            parent();
            selected = first_visible = 0;
            refresh();
            break;
        default:
            if ((event.ch == 'e' || event.ch == 'E') && selected < count &&
                entries[selected].type == TINOS_DIRENT_FILE) {
                char path[PATH_CAP];
                join_path(entries[selected].name, path);
                const char *args[] = { "/apps/gui/editor.elf", path };
                tinos_spawn(args[0], 2, args);
            } else if (event.ch == 'r' || event.ch == 'R')
                refresh();
            else
                continue;
        }
        render();
    }
    tinos_gui_close(ui.handle);
    return 0;
}
