#include <tinos/string.h>
#include <tinos/widgets.h>

static tinos_widget_view_t ui;
static tinos_system_info_t info;
static int category, page = -1, keyboard_layout;
static tinos_net_settings_t network;
static tinos_audio_settings_t audio;
static int wallpaper, active_field = -1;
static bool field_select_all;
static char fields[4][16], message[96];

static void ip_text(uint32_t address, char out[16])
{
    out[0] = '\0';
    for (int i = 3; i >= 0; i--) {
        char digits[11];
        tinos_u32(digits, (address >> (i * 8)) & 255u);
        tinos_strlcpy(out + strlen(out), digits, 16 - strlen(out));
        if (i)
            tinos_strlcpy(out + strlen(out), ".", 16 - strlen(out));
    }
}

static bool parse_ip(const char *text, uint32_t *address)
{
    uint32_t result = 0;
    for (unsigned part = 0; part < 4; part++) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned value = 0, digits = 0;
        while (*text >= '0' && *text <= '9') {
            if (++digits > 3)
                return false;
            value = value * 10 + (unsigned)(*text++ - '0');
        }
        if (value > 255 || (part != 3 && *text++ != '.'))
            return false;
        result = result << 8 | value;
    }
    if (*text)
        return false;
    *address = result;
    return true;
}

static void load_network_fields(void)
{
    ip_text(network.address, fields[0]);
    ip_text(network.netmask, fields[1]);
    ip_text(network.gateway, fields[2]);
    ip_text(network.dns, fields[3]);
    ui.dirty = true;
}

static void status(const char *text)
{
    tinos_strlcpy(message, text, sizeof(message));
    ui.dirty = true;
}

typedef struct {
    const char *name, *summary, *icon;
} panel_page_t;

static const panel_page_t network_pages[] = {
    { "Status", "Current address and adapter state", "info" },
    { "IP Configuration", "DHCP or a manual address", "chip" },
    { "Connectivity", "DNS resolution and ping", "terminal" },
};
static const panel_page_t display[] = {
    { "Resolution", "Screen mode and text scale", "chip" },
    { "Appearance", "Desktop colours and wallpaper", "tinos" },
};
static const panel_page_t sound[] = {
    { "Output", "Audio device and playback settings", "file" },
};
static const panel_page_t keyboard[] = {
    { "Layout", "Keyboard language and keymap", "editor" },
};
static const struct {
    const char *name, *summary, *icon;
    const panel_page_t *pages;
    unsigned count;
} categories[] = {
    { "Network", "Connections, IP and diagnostics", "chip", network_pages, 3 },
    { "Display", "Resolution and desktop presentation", "info", display, 2 },
    { "Sound", "Audio output and volume", "file", sound, 1 },
    { "Keyboard", "Layout and input behaviour", "editor", keyboard, 1 },
};

static int sidebar_width(void)
{
    return ui.width >= 620 ? 210 : 140;
}

static int category_step(void) { return ui.height < 380 ? 28 : 32; }
static int page_step(void) { return ui.height < 380 ? 20 : 24; }

static int category_y(unsigned index)
{
    int y = ui.height < 380 ? 32 : 40;
    for (unsigned i = 0; i < index; i++)
        y += category_step() + (int)categories[i].count * page_step();
    return y;
}

static void value_row(tinos_canvas_t *canvas, int x, int y, int width,
                      const char *label, const char *value,
                      const tinos_gui_theme_t *theme)
{
    tinos_canvas_text(canvas, x, y, 100, label, theme->muted);
    tinos_canvas_text(canvas, x + 104, y, width - 104, value,
                      theme->foreground);
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w = (int)view->width, h = (int)view->height;
    int sidebar = sidebar_width(), content = w - sidebar;
    tinos_canvas_fill(canvas, 0, 0, w, h, theme->background);
    tinos_canvas_fill(canvas, 0, 0, sidebar, h, 0xe1e0e7u);
    tinos_canvas_fill(canvas, sidebar, 0, 1, h, theme->border);
    tinos_canvas_text(canvas, 16, 10, sidebar - 28,
                      "Control Panel", theme->foreground);

    for (unsigned i = 0; i < 4; i++) {
        int y = category_y(i);
        if (y + 28 > h)
            break;
        if ((int)i == category)
            tinos_canvas_fill(canvas, 8, y, sidebar - 16, 28,
                              theme->selection);
        tinos_canvas_icon(canvas, categories[i].icon, 16, y + 5, 18);
        tinos_canvas_text(canvas, 42, y + 6, sidebar - 50,
                          categories[i].name,
                          (int)i == category ? 0xffffffu :
                                               theme->foreground);
        for (unsigned j = 0; j < categories[i].count; j++) {
            int sy = y + category_step() + (int)j * page_step();
            if (sy + page_step() - 2 > h)
                break;
            if ((int)i == category && (int)j == page)
                tinos_canvas_fill(canvas, 24, sy, sidebar - 32,
                                  page_step() - 2,
                                  theme->accent);
            tinos_canvas_icon(canvas, categories[i].pages[j].icon,
                              28, sy + 3, 16);
            tinos_canvas_text(canvas, 49, sy + 3, sidebar - 54,
                              categories[i].pages[j].name,
                              (int)i == category && (int)j == page ?
                              0xffffffu : theme->muted);
        }
    }

    const char *title = page < 0 ? categories[category].name :
                        categories[category].pages[page].name;
    const char *summary = page < 0 ? categories[category].summary :
                          categories[category].pages[page].summary;
    int x = sidebar + 22;
    tinos_canvas_text(canvas, x, 16, content - 44, title, theme->foreground);
    tinos_canvas_text(canvas, x, 36, content - 44, summary, theme->muted);
    tinos_canvas_fill(canvas, sidebar + 18, 54, content - 36, 1,
                      theme->border);
    if (page < 0) {
        unsigned cards = categories[category].count;
        int cols = content >= 500 ? 2 : 1;
        int cw = cols == 2 ? (content - 62) / 2 : content - 48;
        int ch = content >= 500 ? 76 : 65;
        for (unsigned i = 0; i < cards; i++) {
            int cx = sidebar + 24 + (i % cols) * (cw + 14);
            int cy = 76 + (i / cols) * (ch + 12);
            if (cy + ch > h)
                break;
            tinos_canvas_fill(canvas, cx, cy, cw, ch, theme->panel_light);
            tinos_canvas_frame(canvas, cx, cy, cw, ch, theme->border);
            tinos_canvas_icon(canvas, categories[category].pages[i].icon,
                              cx + 14, cy + 16, 32);
            tinos_canvas_text(canvas, cx + 56, cy + 14, cw - 62,
                              categories[category].pages[i].name,
                              theme->foreground);
            tinos_canvas_text(canvas, cx + 56, cy + 34, cw - 62,
                              categories[category].pages[i].summary,
                              theme->muted);
        }
    } else if (category == 3) {
        tinos_canvas_icon(canvas, "editor", x, 78, 32);
        tinos_canvas_text(canvas, x + 44, 83, content - 72,
                          "Keyboard layout", theme->foreground);
        tinos_canvas_text(canvas, x, 130, content - 44,
                          keyboard_layout == TINOS_KBD_ES ?
                          "Spanish (ES)" : "English (US)", theme->foreground);
        tinos_canvas_button(canvas, x, 162, 116, 30, "English", false);
        tinos_canvas_button(canvas, x + 126, 162, 116, 30, "Spanish", false);
        tinos_canvas_text(canvas, x, 211, content - 44,
                          "Press 1 for US or 2 for ES", theme->muted);
    } else if (category == 0 && page == 0) {
        tinos_canvas_text(canvas, x, 84, content - 44,
                          network.available ? "Adapter available" :
                                              "Adapter unavailable",
                          network.available ? theme->foreground :
                                              theme->danger);
        char address[16];
        ip_text(network.address, address);
        value_row(canvas, x, 120, content - 44, "address",
                  network.configured ? address : "not configured", theme);
        ip_text(network.netmask, address);
        value_row(canvas, x, 144, content - 44, "netmask", address, theme);
        ip_text(network.gateway, address);
        value_row(canvas, x, 168, content - 44, "gateway", address, theme);
        ip_text(network.dns, address);
        value_row(canvas, x, 192, content - 44, "DNS", address, theme);
        value_row(canvas, x, 216, content - 44, "mode",
                  network.configured ? "configured" : "offline", theme);
    } else if (category == 0 && page == 1) {
        bool compact = h < 320;
        tinos_canvas_button(canvas, x, compact ? 58 : 72, 115,
                            compact ? 24 : 30, "DHCP", false);
        tinos_canvas_button(canvas, x + 125, compact ? 58 : 72, 115,
                            compact ? 24 : 30, "Manual", false);
        static const char *const labels[4] = {
            "IP address", "Netmask", "Gateway", "DNS",
        };
        static const char *const short_labels[4] = {
            "IP", "Mask", "Gateway", "DNS",
        };
        int gap = compact ? 24 : h < 380 ? 34 : 42;
        for (int i = 0; i < 4; i++) {
            int y = (compact ? 86 : 112) + i * gap;
            int input_x = x + (compact ? 68 : 102);
            tinos_canvas_text(canvas, x, y + 6, compact ? 60 : 96,
                              compact ? short_labels[i] : labels[i],
                              theme->muted);
            tinos_canvas_fill(canvas, input_x, y,
                              content - (compact ? 84 : 152),
                              compact ? 22 : 30,
                              0xf4f3f6u);
            tinos_canvas_frame(canvas, input_x, y,
                               content - (compact ? 84 : 152),
                               compact ? 22 : 30,
                               active_field == i ? theme->accent :
                                                   theme->border);
            tinos_canvas_text(canvas, input_x + 4,
                              y + (compact ? 3 : 6),
                              content - (compact ? 92 : 164),
                              fields[i], theme->foreground);
        }
        int buttons_y = (compact ? 86 : 112) + gap * 4 + 4;
        tinos_canvas_button(canvas, x, buttons_y, 108,
                            compact ? 24 : 28, "Apply", false);
        tinos_canvas_button(canvas, x + 118, buttons_y, 108,
                            compact ? 24 : 28,
                            "Auto DHCP", false);
    } else if (category == 0 && page == 2) {
        tinos_canvas_text(canvas, x, 90, content - 44,
                          "DNS example.com and ping gateway", theme->foreground);
        tinos_canvas_button(canvas, x, 124, 150, 32,
                            "Run diagnostics", false);
    } else if (category == 1 && page == 0) {
        char number[11];
        tinos_u32(number, info.screen_width);
        tinos_canvas_text(canvas, x, 90, content - 44,
                          "Screen resolution", theme->foreground);
        tinos_canvas_text(canvas, x, 118, content - 44, number,
                          theme->foreground);
        tinos_u32(number, info.screen_height);
        tinos_canvas_text(canvas, x + 52, 118, content - 96, "x",
                          theme->muted);
        tinos_canvas_text(canvas, x + 72, 118, content - 116, number,
                          theme->foreground);
        tinos_canvas_button(canvas, x, 170, 110, 32, "1024 x 768", false);
        tinos_canvas_button(canvas, x + 120, 170, 110, 32,
                            "800 x 600", false);
    } else if (category == 1 && page == 1) {
        tinos_canvas_text(canvas, x, 90, content - 44,
                          "Desktop wallpaper", theme->foreground);
        tinos_canvas_button(canvas, x, 130, 96, 32, "Default",
                            wallpaper == TINOS_WALLPAPER_DEFAULT);
        tinos_canvas_button(canvas, x + 106, 130, 86, 32, "Solid",
                            wallpaper == TINOS_WALLPAPER_SOLID);
        tinos_canvas_button(canvas, x + 202, 130, 96, 32, "Gradient",
                            wallpaper == TINOS_WALLPAPER_GRADIENT);
    } else if (category == 2 && page == 0) {
        char level[11];
        tinos_u32(level, audio.volume);
        tinos_canvas_text(canvas, x, 90, content - 44,
                          "Output volume", theme->foreground);
        tinos_canvas_text(canvas, x + 142, 90, 80, level,
                          theme->foreground);
        tinos_canvas_button(canvas, x, 146, 64, 32, "-", false);
        tinos_canvas_button(canvas, x + 74, 146, 64, 32, "+", false);
        tinos_canvas_button(canvas, x + 148, 146, 92, 32,
                            audio.muted ? "Unmute" : "Mute", false);
        tinos_canvas_fill(canvas, x, 198, content - 64, 14, theme->border);
        tinos_canvas_fill(canvas, x + 2, 200,
                          (content - 68) * (int)audio.volume / 100,
                          10, theme->accent_light);
        tinos_canvas_frame(canvas, x, 198, content - 64, 14, theme->border);
    } else {
        tinos_canvas_text(canvas, x, 92, content - 44,
                          "Select an action on the left.", theme->muted);
    }
    if (message[0])
        tinos_canvas_text(canvas, x,
                          category == 0 && page == 1 && h < 320 ?
                          h - 18 : h - 25, content - 44,
                          message, theme->muted);
}

static void show_settings(void)
{
    tinos_sysinfo(&info);
    keyboard_layout = tinos_ioctl(0, TINOS_IOCTL_KBD_GET_LAYOUT, 0);
    tinos_ioctl(0, TINOS_IOCTL_NET_GET, (unsigned)(uintptr_t)&network);
    tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET, (unsigned)(uintptr_t)&audio);
    wallpaper = tinos_ioctl(0, TINOS_IOCTL_WALLPAPER_GET, 0);
    ui.dirty = true;
    tinos_widget_flush(&ui);
}

static void apply_manual(void)
{
    tinos_net_settings_t settings = network;
    uint32_t *values[] = {
        &settings.address, &settings.netmask,
        &settings.gateway, &settings.dns,
    };
    for (unsigned i = 0; i < 4; i++)
        if (!parse_ip(fields[i], values[i])) {
            status("Invalid IPv4 address");
            return;
        }
    if (tinos_ioctl(0, TINOS_IOCTL_NET_MANUAL,
                    (unsigned)(uintptr_t)&settings) < 0)
        status("Network unavailable or invalid netmask");
    else {
        status("Manual network configuration applied");
        show_settings();
    }
}

static void request_dhcp(void)
{
    if (tinos_ioctl(0, TINOS_IOCTL_NET_DHCP, 0) < 0)
        status("DHCP request failed");
    else {
        status("DHCP lease acquired");
        show_settings();
        load_network_fields();
    }
}

static void diagnostics(void)
{
    tinos_dns_probe_t probe = { .host = "example.com" };
    bool dns = tinos_ioctl(0, TINOS_IOCTL_NET_DNS,
                           (unsigned)(uintptr_t)&probe) == 0;
    int ping = network.gateway ?
        tinos_ioctl(0, TINOS_IOCTL_NET_PING, network.gateway) : -1;
    status(dns && ping >= 0 ? "DNS OK, gateway ping OK" :
           dns ? "DNS OK, gateway ping failed" :
           ping >= 0 ? "DNS failed, gateway ping OK" :
                       "DNS and gateway ping failed");
}

static void change_audio(int step, bool toggle_mute)
{
    if (tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET,
                    (unsigned)(uintptr_t)&audio) < 0)
        return;
    int level = (int)audio.volume + step;
    audio.volume = (unsigned)(level < 0 ? 0 : level > 100 ? 100 : level);
    if (toggle_mute)
        audio.muted = !audio.muted;
    if (tinos_ioctl(0, TINOS_IOCTL_AUDIO_SET,
                    (unsigned)(uintptr_t)&audio) < 0)
        status("Audio device unavailable");
    show_settings();
}

int main(void)
{
    tinos_system_info_t system;
    bool roomy = tinos_sysinfo(&system) == 0 &&
                 system.memory_total_kib >= 12288;
    if (tinos_widget_open(&ui, "Control Panel",
                          roomy ? 720 : 480, roomy ? 470 : 350) < 0)
        return 1;
    ui.on_paint = paint;
    show_settings();
    load_network_fields();
    ui.dirty = true;
    tinos_widget_flush(&ui);
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE && active_field < 0))
            break;
        if (event.type == TINOS_GUI_RESIZE)
            show_settings();
        if (event.type == TINOS_GUI_MOUSE && event.code == 1) {
            int sidebar = sidebar_width();
            if (event.x < sidebar) {
                for (unsigned i = 0; i < 4; i++) {
                    int y = category_y(i);
                    if (event.y >= y && event.y < y + 28) {
                        category = (int)i;
                        page = -1;
                        active_field = -1;
                        message[0] = '\0';
                        show_settings();
                        break;
                    }
                    if (event.y >= y + category_step() &&
                        event.y < y + category_step() +
                                  (int)categories[i].count * page_step()) {
                        category = (int)i;
                        page = (event.y - y - category_step()) / page_step();
                        active_field = -1;
                        message[0] = '\0';
                        show_settings();
                        if (category == 0 && page == 1)
                            load_network_fields();
                        ui.dirty = true;
                        break;
                    }
                }
            } else if (page < 0 && event.y >= 76) {
                int content = ui.width - sidebar;
                int cols = content >= 500 ? 2 : 1;
                int cw = cols == 2 ? (content - 62) / 2 : content - 48;
                int ch = content >= 500 ? 76 : 65;
                int col = cols == 2 && event.x >= sidebar + 24 + cw + 14;
                int row = (event.y - 76) / (ch + 12);
                int item = row * cols + col;
                if (item >= 0 && item < (int)categories[category].count) {
                    page = item;
                    active_field = -1;
                    message[0] = '\0';
                    show_settings();
                    if (category == 0 && page == 1)
                        load_network_fields();
                    ui.dirty = true;
                }
            } else if (category == 0 && page == 1) {
                bool compact = ui.height < 320;
                int x = sidebar + 22, gap = compact ? 24 :
                                          ui.height < 380 ? 34 : 42;
                if (event.y >= (compact ? 58 : 72) &&
                    event.y < (compact ? 82 : 102)) {
                    if (event.x >= x && event.x < x + 115)
                        request_dhcp();
                    else if (event.x >= x + 125 && event.x < x + 240)
                        status("Enter IPv4 values, then Apply");
                }
                for (int i = 0; i < 4; i++)
                    if (event.x >= x + (compact ? 68 : 102) &&
                        event.x < (int)ui.width - 12 &&
                        event.y >= (compact ? 86 : 112) + i * gap &&
                        event.y < (compact ? 108 : 142) + i * gap) {
                        active_field = i;
                        field_select_all = true;
                    }
                int buttons_y = (compact ? 86 : 112) + gap * 4 + 4;
                if (event.y >= buttons_y &&
                    event.y < buttons_y + (compact ? 24 : 28)) {
                    if (event.x >= x && event.x < x + 108)
                        apply_manual();
                    else if (event.x >= x + 118 && event.x < x + 226)
                        request_dhcp();
                }
                show_settings();
            } else if (category == 0 && page == 2 &&
                       event.x >= sidebar + 22 &&
                       event.x < sidebar + 172 &&
                       event.y >= 124 && event.y < 156) {
                diagnostics();
                show_settings();
            } else if (category == 1 && page == 0 &&
                       event.y >= 170 && event.y < 202) {
                unsigned mode = event.x >= sidebar + 22 + 120 ?
                                (800u << 16 | 600u) :
                                (1024u << 16 | 768u);
                if (tinos_ioctl(0, TINOS_IOCTL_DISPLAY_MODE, mode) < 0)
                    status("Display mode unavailable");
                else
                    status("Display mode changed");
                show_settings();
            } else if (category == 1 && page == 1 &&
                       event.y >= 130 && event.y < 162) {
                unsigned mode = event.x >= sidebar + 22 + 202 ?
                    TINOS_WALLPAPER_GRADIENT :
                    event.x >= sidebar + 22 + 106 ?
                    TINOS_WALLPAPER_SOLID : TINOS_WALLPAPER_DEFAULT;
                if (tinos_ioctl(0, TINOS_IOCTL_WALLPAPER, mode) < 0)
                    status("Wallpaper unavailable");
                else
                    status("Wallpaper applied");
                show_settings();
            } else if (category == 2 && page == 0) {
                int x = sidebar + 22;
                if (event.y >= 146 && event.y < 178) {
                    if (event.x >= x && event.x < x + 64)
                        change_audio(-10, false);
                    else if (event.x >= x + 74 && event.x < x + 138)
                        change_audio(10, false);
                    else if (event.x >= x + 148 && event.x < x + 240)
                        change_audio(0, true);
                } else if (event.y >= 198 && event.y < 212 &&
                           event.x >= x && event.x < (int)ui.width - 42) {
                    if (tinos_ioctl(0, TINOS_IOCTL_AUDIO_GET,
                                    (unsigned)(uintptr_t)&audio) == 0) {
                        audio.volume = (event.x - x) * 100 /
                                       (ui.width - sidebar - 64);
                        tinos_ioctl(0, TINOS_IOCTL_AUDIO_SET,
                                    (unsigned)(uintptr_t)&audio);
                        show_settings();
                    }
                }
            } else if (category == 3 && page == 0 &&
                       event.y >= 162 && event.y < 192) {
                int key = event.x >= sidebar + 22 + 126 ?
                          TINOS_KBD_ES : TINOS_KBD_US;
                tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT, key);
                show_settings();
            }
        }
        if (event.type == TINOS_GUI_KEY && active_field >= 0) {
            if (event.code == TINOS_KEY_ESCAPE)
                active_field = -1;
            else if (event.code == TINOS_KEY_TAB) {
                active_field = (active_field + 1) % 4;
                field_select_all = true;
            } else if ((event.mods & TINOS_MOD_CTRL) &&
                       (event.ch == 'a' || event.ch == 'A')) {
                field_select_all = true;
            } else if (event.code == TINOS_KEY_ENTER)
                apply_manual();
            else if (event.code == TINOS_KEY_BACKSPACE) {
                size_t len = strlen(fields[active_field]);
                if (field_select_all)
                    fields[active_field][0] = '\0';
                else if (len)
                    fields[active_field][len - 1] = '\0';
                field_select_all = false;
            } else if ((event.ch >= '0' && event.ch <= '9') ||
                       event.ch == '.') {
                if (field_select_all)
                    fields[active_field][0] = '\0';
                field_select_all = false;
                size_t len = strlen(fields[active_field]);
                if (len + 1 < sizeof(fields[0])) {
                    fields[active_field][len] = event.ch;
                    fields[active_field][len + 1] = '\0';
                }
            }
            show_settings();
            continue;
        }
        if (event.type == TINOS_GUI_KEY && category == 2 && page == 0) {
            if (event.ch == '+')
                change_audio(10, false);
            else if (event.ch == '-')
                change_audio(-10, false);
            else if (event.ch == 'm' || event.ch == 'M')
                change_audio(0, true);
        }
        if (event.type == TINOS_GUI_KEY &&
            (event.ch == '1' || event.ch == '2')) {
            tinos_ioctl(0, TINOS_IOCTL_KBD_SET_LAYOUT,
                        event.ch == '1' ? TINOS_KBD_US : TINOS_KBD_ES);
            category = 3;
            page = 0;
            show_settings();
        }
    }
    tinos_widget_close(&ui);
    return 0;
}
