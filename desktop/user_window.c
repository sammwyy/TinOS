#include <tinos/arch.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/process.h>
#include <tinos/string.h>
#include <tinos/ui.h>
#include <tinos/user_window.h>

#define GUI_QUEUE 16
#define GUI_CONSOLE_BUFFER 4096

typedef struct {
    uint32_t pid;
    window_t *window;
    char cells[TINOS_GUI_TEXT_SIZE];
    size_t columns, rows;
    tinos_gui_event_t events[GUI_QUEUE];
    unsigned head, tail;
    char console[GUI_CONSOLE_BUFFER];
    unsigned console_head, console_tail;
    bool console_attached;
    uint32_t *pixels;
    uint32_t pixel_width, pixel_height;
    uint32_t *surface;
    uint32_t surface_width, surface_height;
    uint32_t timer_interval;
    uint64_t next_timer;
} gui_client_t;

static gui_client_t clients[PROCESS_MAX];
static bool available;

static gui_client_t *client_for(uint32_t pid)
{
    for (size_t i = 0; i < ARRAY_SIZE(clients); i++)
        if (clients[i].pid == pid && pid)
            return &clients[i];
    return NULL;
}

static void enqueue(gui_client_t *client, tinos_gui_event_t event)
{
    unsigned next = (client->tail + 1) % GUI_QUEUE;
    if (next == client->head) {
        if (event.type == TINOS_GUI_KEY || event.type == TINOS_GUI_MOUSE ||
            event.type == TINOS_GUI_RESIZE || event.type == TINOS_GUI_TIMER)
            return;
        client->head = (client->head + 1) % GUI_QUEUE;
    }
    client->events[client->tail] = event;
    client->tail = next;
    process_gui_wake(client->pid);
}

static void user_draw(window_t *window, rect_t area)
{
    gui_client_t *client = window->priv;
    gfx_fill_rect(area.x, area.y, area.w, area.h, COLOR_TERMINAL_BG);
    if (client->surface) {
        gfx_blit_image(rect_make(area.x, area.y, client->surface_width,
                                 client->surface_height),
                       client->surface, client->surface_width,
                       client->surface_height);
        return;
    }
    if (client->pixels)
        gfx_blit_image(rect_make(area.x + 8, area.y + 2 * FONT_H,
                                client->pixel_width, client->pixel_height),
                       client->pixels, client->pixel_width, client->pixel_height);
    for (size_t row = 0; row < client->rows && (int)(row + 1) * FONT_H <= area.h;
         row++) {
        char line[TINOS_GUI_COLS + 1];
        size_t columns = MIN(client->columns, (size_t)area.w / FONT_W);
        for (size_t col = 0; col < columns; col++) {
            unsigned char c = client->cells[row * client->columns + col];
            line[col] = c >= 32 && c < 127 ? c : ' ';
        }
        line[columns] = '\0';
        gfx_text(area.x, area.y + (int)row * FONT_H, line, COLOR_TERMINAL_TEXT);
    }
}

static bool user_key(window_t *window, const key_event_t *key)
{
    gui_client_t *client = window->priv;
    enqueue(client, (tinos_gui_event_t){
        .type = TINOS_GUI_KEY, .code = key->code, .ch = (uint8_t)key->ch,
        .mods = key->mods,
    });
    return true;
}

static bool user_mouse(window_t *window, const mouse_event_t *mouse, int x, int y)
{
    if (mouse->pressed)
        enqueue(window->priv, (tinos_gui_event_t){
            .type = TINOS_GUI_MOUSE, .code = mouse->pressed, .x = x, .y = y,
        });
    return true;
}

static void user_resize(window_t *window, rect_t area)
{
    enqueue(window->priv, (tinos_gui_event_t){
        .type = TINOS_GUI_RESIZE, .x = area.w / FONT_W,
        .y = area.h / FONT_H,
    });
}

static bool user_tick(window_t *window)
{
    gui_client_t *client = window->priv;
    uint64_t now = timer_ticks();
    if (client->timer_interval && now >= client->next_timer) {
        client->next_timer = now + client->timer_interval;
        enqueue(client, (tinos_gui_event_t){ .type = TINOS_GUI_TIMER });
    }
    return false;
}

static void user_close(window_t *window)
{
    gui_client_t *client = window->priv;
    client->window = NULL;
    client->console_attached = false;
    process_wake_console_output(client->pid);
    kfree(client->pixels);
    client->pixels = NULL;
    kfree(client->surface);
    client->surface = NULL;
    client->head = client->tail = 0;
    enqueue(client, (tinos_gui_event_t){ .type = TINOS_GUI_CLOSE });
}

static const app_class_t user_class = {
    .name = "Userspace window", .on_draw = user_draw,
    .on_key = user_key, .on_mouse = user_mouse,
    .on_resize = user_resize, .on_close = user_close, .on_tick = user_tick,
};

void desktop_user_window_init(void)
{
    memset(clients, 0, sizeof(clients));
    available = true;
}

int desktop_user_window_open(uint32_t pid, const char *title, int width, int height)
{
    if (!available || !pid || client_for(pid) ||
        width < WINDOW_MIN_W || width > 800 ||
        height < WINDOW_MIN_H || height > 600)
        return -1;
    gui_client_t *client = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(clients); i++)
        if (!clients[i].pid) {
            client = &clients[i];
            break;
        }
    if (!client)
        return -1;
    memset(client, 0, sizeof(*client));
    client->pid = pid;
    window_t *window = wm_open(&user_class, width, height, title, NULL);
    if (!window) {
        client->pid = 0;
        return -1;
    }
    client->window = window;
    window->priv = client;
    return 1;
}

int desktop_user_window_present(uint32_t pid, int handle, const char *cells,
                                size_t length, size_t columns)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window ||
        !columns || columns > TINOS_GUI_COLS ||
        !length || length > TINOS_GUI_TEXT_SIZE ||
        length % columns || length / columns > TINOS_GUI_ROWS)
        return -1;
    memcpy(client->cells, cells, length);
    client->columns = columns;
    client->rows = length / columns;
    wm_invalidate(client->window);
    return 0;
}

int desktop_user_window_next(uint32_t pid, int handle, tinos_gui_event_t *event)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client)
        return -1;
    if (client->head == client->tail)
        return 0;
    *event = client->events[client->head];
    client->head = (client->head + 1) % GUI_QUEUE;
    return 1;
}

int desktop_user_window_close(uint32_t pid, int handle)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window)
        return -1;
    desktop_user_window_release(pid);
    return 0;
}

void desktop_user_window_release(uint32_t pid)
{
    gui_client_t *client = client_for(pid);
    if (!client)
        return;
    if (client->window)
        wm_close(client->window);
    kfree(client->pixels);
    kfree(client->surface);
    memset(client, 0, sizeof(*client));
}

int desktop_user_window_size(uint32_t pid, int handle,
                             tinos_gui_size_t *size)
{
    gui_client_t *client = client_for(pid);
    if (!size || handle != 1 || !client || !client->window)
        return -1;
    rect_t area = wm_client_rect(client->window);
    size->width = area.w;
    size->height = area.h;
    return 0;
}

int desktop_user_window_surface_config(uint32_t pid, int handle,
                                       uint32_t width, uint32_t height)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window ||
        !width || !height ||
        width > TINOS_GUI_SURFACE_MAX_WIDTH ||
        height > TINOS_GUI_SURFACE_MAX_HEIGHT)
        return -1;
    rect_t area = wm_client_rect(client->window);
    if (width > (uint32_t)area.w || height > (uint32_t)area.h)
        return -1;
    if (client->surface && client->surface_width == width &&
        client->surface_height == height)
        return 0;
    size_t bytes = (size_t)width * height * sizeof(uint32_t);
    uint32_t *replacement = krealloc(client->surface, bytes);
    if (!replacement)
        return -1;
    memset(replacement, 0, bytes);
    client->surface = replacement;
    client->surface_width = width;
    client->surface_height = height;
    wm_invalidate(client->window);
    return 0;
}

int desktop_user_window_surface_tile(uint32_t pid,
                                     const tinos_gui_tile_t *tile,
                                     const uint32_t *pixels)
{
    gui_client_t *client = client_for(pid);
    if (!tile || !pixels || tile->handle != 1 || !client ||
        !client->window || !client->surface ||
        !tile->width || !tile->height ||
        tile->width > TINOS_GUI_TILE_MAX_WIDTH ||
        tile->height > TINOS_GUI_TILE_MAX_HEIGHT ||
        tile->x >= client->surface_width ||
        tile->y >= client->surface_height ||
        tile->width > client->surface_width - tile->x ||
        tile->height > client->surface_height - tile->y)
        return -1;
    for (unsigned row = 0; row < tile->height; row++)
        for (unsigned col = 0; col < tile->width; col++)
            client->surface[(size_t)(tile->y + row) *
                            client->surface_width + tile->x + col] =
                pixels[(size_t)row * tile->width + col] | 0xff000000u;
    wm_invalidate(client->window);
    return 0;
}

void desktop_user_window_theme(tinos_gui_theme_t *theme)
{
    *theme = (tinos_gui_theme_t){
        .revision = 2,
        .background = COLOR_WINDOW_BG, .surface = COLOR_PANEL_LIGHT,
        .foreground = COLOR_TEXT, .muted = COLOR_TEXT_DIM,
        .accent = COLOR_ACCENT_DARK, .accent_text = RGB(0xff, 0xff, 0xff),
        .border = COLOR_BORDER, .selection = COLOR_SELECTION,
        .danger = COLOR_DANGER,
        .panel = COLOR_PANEL, .panel_light = COLOR_PANEL_LIGHT,
        .terminal_bg = COLOR_TERMINAL_BG,
        .terminal_text = COLOR_TERMINAL_TEXT,
        .folder = COLOR_FOLDER, .file = COLOR_FILE,
        .accent_light = COLOR_ACCENT,
    };
}

int desktop_user_window_blit(uint32_t pid, int handle, const uint32_t *pixels,
                             uint32_t width, uint32_t height)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window || !width || !height ||
        width > TINOS_GUI_IMAGE_WIDTH || height > TINOS_GUI_IMAGE_HEIGHT)
        return -1;
    if (width != client->pixel_width || height != client->pixel_height) {
        uint32_t *replacement = kmalloc((size_t)width * height * sizeof(uint32_t));
        if (!replacement)
            return -1;
        kfree(client->pixels);
        client->pixels = replacement;
        client->pixel_width = width;
        client->pixel_height = height;
    }
    memcpy(client->pixels, pixels, (size_t)width * height * sizeof(uint32_t));
    wm_invalidate(client->window);
    return 0;
}

int desktop_user_window_timer(uint32_t pid, int handle, uint32_t interval_ms)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window ||
        (interval_ms && (interval_ms < 20 || interval_ms > 1000)))
        return -1;
    client->timer_interval = interval_ms;
    client->next_timer = timer_ticks() + interval_ms;
    return 0;
}

int desktop_user_window_console_attach(uint32_t pid, int handle)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window)
        return -1;
    client->console_attached = true;
    return 0;
}

int desktop_user_window_console_write(uint32_t pid, const char *data,
                                      size_t length)
{
    gui_client_t *client = client_for(pid);
    if (!client || !client->window || !client->console_attached)
        return -1;
    bool empty = client->console_head == client->console_tail;
    size_t written = 0;
    while (written < length) {
        unsigned next = (client->console_tail + 1) % GUI_CONSOLE_BUFFER;
        if (next == client->console_head)
            break;
        client->console[client->console_tail] = data[written++];
        client->console_tail = next;
    }
    if (empty && written)
        enqueue(client, (tinos_gui_event_t){ .type = TINOS_GUI_OUTPUT });
    return (int)written;
}

int desktop_user_window_console_read(uint32_t pid, int handle,
                                     char *buffer, size_t length)
{
    gui_client_t *client = client_for(pid);
    if (handle != 1 || !client || !client->window || !client->console_attached)
        return -1;
    size_t count = 0;
    while (count < length && client->console_head != client->console_tail) {
        buffer[count++] = client->console[client->console_head];
        client->console_head = (client->console_head + 1) % GUI_CONSOLE_BUFFER;
    }
    if (count)
        process_wake_console_output(pid);
    return (int)count;
}

void desktop_user_window_child_exit(uint32_t pid, uint32_t child, int status)
{
    gui_client_t *client = client_for(pid);
    if (client && client->window)
        enqueue(client, (tinos_gui_event_t){
            .type = TINOS_GUI_CHILD_EXIT, .code = child, .x = status,
        });
}
