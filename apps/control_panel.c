/* Control Panel.  The shell and desktop deliberately expose only a small
 * public API, so each section owns its layout here and can grow independently
 * as more driver settings become available. */

#include <tinos/arch.h>
#include <tinos/audio.h>
#include <tinos/bus.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/timer.h>
#include <tinos/ui.h>

#define PANEL_SIDEBAR_W 210
#define PANEL_HEADER_H   54
#define PANEL_ROW_H      30
#define PANEL_CARD_W     250
#define PANEL_CARD_H     76

typedef struct {
    const char *name;
    const char *summary;
    icon_t icon;
} panel_subcategory_t;

typedef struct {
    const char *name;
    const char *summary;
    icon_t icon;
    const panel_subcategory_t *subcategories;
    size_t subcategory_count;
} panel_category_t;

static const panel_subcategory_t network_pages[] = {
    { "Status", "Current address and adapter state", ICON_INFO },
    { "IP Configuration", "Automatic DHCP or a manual address", ICON_CHIP },
    { "Connectivity", "Test DNS resolution and ping", ICON_TERMINAL },
};
static const panel_subcategory_t display_pages[] = {
    { "Resolution", "Screen mode and text scale", ICON_CHIP },
    { "Appearance", "Desktop colours, gradients and wallpaper", ICON_TINOS },
};
static const panel_subcategory_t sound_pages[] = {
    { "Output", "Audio device and playback settings", ICON_FILE },
};
static const panel_subcategory_t keyboard_pages[] = {
    { "Layout", "Keyboard language and keymap", ICON_EDITOR },
};

static const panel_category_t categories[] = {
    { "Network", "Connections, IP and diagnostics", ICON_CHIP,
      network_pages, ARRAY_SIZE(network_pages) },
    { "Display", "Resolution and desktop presentation", ICON_INFO,
      display_pages, ARRAY_SIZE(display_pages) },
    { "Sound", "Audio output and volume", ICON_FILE,
      sound_pages, ARRAY_SIZE(sound_pages) },
    { "Keyboard", "Layout and input behaviour", ICON_EDITOR,
      keyboard_pages, ARRAY_SIZE(keyboard_pages) },
};

typedef struct {
    window_t *window;
    int category;
    int subcategory; /* -1 means the category mosaic */
    int active_field; /* Network IP page: 0..3, or -1 */
    bool manual;
    bool blink_on;
    uint64_t blink_at;
    text_input_t fields[4];
    char status[160];
} control_panel_t;

static void panel_sync_network(control_panel_t *panel);
static void panel_status(control_panel_t *panel, const char *message);

static void panel_bus_event(const bus_event_t *event, void *arg)
{
    control_panel_t *panel = arg;
    if (event->type == BUS_NETWORK_CHANGED)
        panel_sync_network(panel);
    panel_status(panel, "Configuration changed");
    wm_invalidate(panel->window);
}

static void panel_status(control_panel_t *panel, const char *message)
{
    strlcpy(panel->status, message, sizeof(panel->status));
}

static void panel_input_set(text_input_t *input, const char *value)
{
    text_input_reset(input, value);
    input->active = false;
    input->select_all = false;
}

static void panel_sync_network(control_panel_t *panel)
{
    const net_config_t *config = net_config();
    char value[24];
    net_format_address(config->address, value, sizeof(value));
    panel_input_set(&panel->fields[0], config->address ? value : "");
    net_format_address(config->netmask, value, sizeof(value));
    panel_input_set(&panel->fields[1], config->netmask ? value : "");
    net_format_address(config->gateway, value, sizeof(value));
    panel_input_set(&panel->fields[2], config->gateway ? value : "");
    net_format_address(config->dns, value, sizeof(value));
    panel_input_set(&panel->fields[3], config->dns ? value : "");
}

static int panel_sidebar_height(int category)
{
    return 32 + (int)categories[category].subcategory_count * 24;
}

static int panel_category_y(int category)
{
    int y = 40;
    for (int i = 0; i < category; i++)
        y += panel_sidebar_height(i);
    return y;
}

static rect_t panel_category_rect(int category)
{
    return rect_make(8, panel_category_y(category), PANEL_SIDEBAR_W - 16, 28);
}

static rect_t panel_subcategory_rect(int category, int subcategory)
{
    return rect_make(24, panel_category_y(category) + 32 + subcategory * 24,
                     PANEL_SIDEBAR_W - 32, 22);
}

static rect_t panel_content_rect(rect_t client)
{
    return rect_make(client.x + PANEL_SIDEBAR_W + 1, client.y,
                     client.w - PANEL_SIDEBAR_W - 1, client.h);
}

static void panel_text_clipped(rect_t clip, int x, int y, const char *text,
                               color_t color)
{
    gfx_clip_push(clip);
    gfx_text(x, y, text, color);
    gfx_clip_pop();
}

static void panel_text_wrapped(rect_t clip, int x, int y, const char *text,
                               color_t color, int max_lines)
{
    int max_chars = clip.w / FONT_W;
    if (max_chars < 1)
        return;
    size_t start = 0;
    size_t length = strlen(text);
    for (int line = 0; line < max_lines && start < length; line++) {
        size_t end = MIN(length, start + (size_t)max_chars);
        if (end < length) {
            size_t split = end;
            while (split > start && text[split] != ' ')
                split--;
            if (split > start)
                end = split;
        }
        gfx_clip_push(clip);
        gfx_text_n(x, y + line * FONT_H, text + start, end - start, color);
        gfx_clip_pop();
        start = end;
        while (start < length && text[start] == ' ')
            start++;
    }
}

static void panel_draw_sidebar(control_panel_t *panel, rect_t client)
{
    gfx_fill_rect(client.x, client.y, PANEL_SIDEBAR_W, client.h,
                  RGB(0xe1, 0xe0, 0xe7));
    gfx_vline(client.x + PANEL_SIDEBAR_W, client.y, client.h, COLOR_BORDER);
    panel_text_clipped(rect_make(client.x + 16, client.y + 10,
                                 PANEL_SIDEBAR_W - 28, FONT_H),
                       client.x + 16, client.y + 10, "Control Panel", COLOR_TEXT);

    for (int i = 0; i < (int)ARRAY_SIZE(categories); i++) {
        rect_t category = panel_category_rect(i);
        category.x += client.x;
        if (i == panel->category) {
            gfx_fill_rect(category.x, client.y + category.y, category.w,
                          category.h, COLOR_SELECTION);
            gfx_rect(category.x, client.y + category.y, category.w,
                     category.h, COLOR_ACCENT);
        }
        widget_icon(category.x + 8, client.y + category.y + 5, 18,
                    categories[i].icon);
        panel_text_clipped(rect_make(category.x + 34, client.y + category.y,
                                     category.w - 42, category.h),
                           category.x + 34, client.y + category.y + 6,
                           categories[i].name,
                           i == panel->category ? RGB(0xff, 0xff, 0xff) :
                                                  COLOR_TEXT);

        {
            for (int s = 0; s < (int)categories[i].subcategory_count; s++) {
                rect_t sub = panel_subcategory_rect(i, s);
                sub.x += client.x;
                if (i == panel->category && s == panel->subcategory) {
                    gfx_fill_rect(sub.x, client.y + sub.y, sub.w, sub.h,
                                  COLOR_ACCENT_DARK);
                    gfx_rect(sub.x, client.y + sub.y, sub.w, sub.h,
                             COLOR_ACCENT);
                }
                widget_icon(sub.x + 4, client.y + sub.y + 3, 16,
                            categories[i].subcategories[s].icon);
                panel_text_clipped(rect_make(sub.x + 25, client.y + sub.y,
                                             sub.w - 29, sub.h),
                                   sub.x + 25, client.y + sub.y + 3,
                                   categories[i].subcategories[s].name,
                                   i == panel->category && s == panel->subcategory ?
                                       RGB(0xff, 0xff, 0xff) : COLOR_TEXT_DIM);
            }
        }
    }
}

static void panel_draw_header(rect_t content, const char *title,
                              const char *summary)
{
    panel_text_clipped(rect_make(content.x + 22, content.y + 16,
                                 content.w - 44, FONT_H),
                       content.x + 22, content.y + 16, title, COLOR_TEXT);
    panel_text_clipped(rect_make(content.x + 22, content.y + 36,
                                 content.w - 44, FONT_H),
                       content.x + 22, content.y + 36, summary, COLOR_TEXT_DIM);
    gfx_hline(content.x + 18, content.y + PANEL_HEADER_H,
              content.w - 36, COLOR_BORDER);
}

static void panel_draw_mosaic(control_panel_t *panel, rect_t content)
{
    const panel_category_t *category = &categories[panel->category];
    panel_draw_header(content, category->name, category->summary);
    for (size_t i = 0; i < category->subcategory_count; i++) {
        int col = (int)(i % 2);
        int row = (int)(i / 2);
        rect_t card = rect_make(content.x + 24 + col * (PANEL_CARD_W + 14),
                                content.y + 76 + row * (PANEL_CARD_H + 12),
                                PANEL_CARD_W, PANEL_CARD_H);
        widget_panel(card, COLOR_PANEL_LIGHT, COLOR_BORDER);
        widget_icon(card.x + 14, card.y + 20, 32,
                    category->subcategories[i].icon);
        panel_text_clipped(rect_make(card.x + 58, card.y + 18,
                                     card.w - 68, FONT_H),
                           card.x + 58, card.y + 18,
                           category->subcategories[i].name, COLOR_TEXT);
        panel_text_wrapped(rect_make(card.x + 58, card.y + 38,
                                     card.w - 68, FONT_H * 2),
                           card.x + 58, card.y + 38,
                           category->subcategories[i].summary, COLOR_TEXT_DIM, 2);
    }
}

static void panel_row(int x, int *y, const char *label, const char *value)
{
    gfx_text(x, *y, label, COLOR_TEXT_DIM);
    gfx_text(x + 150, *y, value, COLOR_TEXT);
    *y += FONT_H + 8;
}

static void panel_draw_network_status(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Network status", "Adapter state and current network values");
    const net_config_t *config = net_config();
    char address[24], mask[24], gateway[24], dns[24];
    net_format_address(config->address, address, sizeof(address));
    net_format_address(config->netmask, mask, sizeof(mask));
    net_format_address(config->gateway, gateway, sizeof(gateway));
    net_format_address(config->dns, dns, sizeof(dns));
    int y = content.y + 84;
    gfx_text(content.x + 24, y, net_available() ? "Adapter available" :
             "Adapter unavailable", net_available() ? COLOR_OK : COLOR_DANGER);
    y += 30;
    panel_row(content.x + 24, &y, "address", config->address ? address : "not configured");
    panel_row(content.x + 24, &y, "netmask", config->netmask ? mask : "-");
    panel_row(content.x + 24, &y, "gateway", config->gateway ? gateway : "-");
    panel_row(content.x + 24, &y, "DNS", config->dns ? dns : "-");
    panel_row(content.x + 24, &y, "mode", panel->manual ? "manual" : "DHCP");
    if (panel->status[0])
        gfx_text(content.x + 24, content.y + content.h - 34, panel->status,
                 COLOR_TEXT_DIM);
}

static void panel_draw_network_ip(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "IP configuration", "Choose DHCP or enter a static IPv4 configuration");
    rect_t automatic = rect_make(content.x + 24, content.y + 72, 130, 30);
    rect_t manual = rect_make(content.x + 164, content.y + 72, 130, 30);
    widget_button(automatic, "Automatic DHCP", !panel->manual, false);
    widget_button(manual, "Manual", panel->manual, false);
    int y = content.y + 116;
    const char *labels[] = { "IP address", "Netmask", "Gateway", "DNS" };
    for (int i = 0; i < 4; i++, y += 42) {
        rect_t field = rect_make(content.x + 24, y, 310, 32);
        text_input_draw(&panel->fields[i], field, labels[i], panel->blink_on);
    }
    widget_button(rect_make(content.x + 24, y + 8, 142, 30), "Apply manual", false, false);
    widget_button(rect_make(content.x + 176, y + 8, 142, 30), "Request DHCP", false, false);
    if (panel->status[0])
        gfx_text(content.x + 24, y + 56, panel->status, COLOR_TEXT_DIM);
}

static void panel_draw_network_connectivity(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Connectivity", "Verify DNS and reachability from the current configuration");
    gfx_text(content.x + 24, content.y + 86,
             "The tests use the configured DNS server and gateway.", COLOR_TEXT_DIM);
    widget_button(rect_make(content.x + 24, content.y + 116, 150, 32),
                  "Run DNS + ping", false, false);
    if (panel->status[0])
        gfx_text(content.x + 24, content.y + 174, panel->status, COLOR_TEXT);
}

static void panel_draw_display_resolution(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Resolution", "Change the active framebuffer mode");
    char current[48];
    ksnprintf(current, sizeof(current), "Current: %dx%d", gfx_width(), gfx_height());
    gfx_text(content.x + 24, content.y + 84, current, COLOR_TEXT);
    widget_button(rect_make(content.x + 24, content.y + 112, 140, 32),
                  "1024 x 768", false, false);
    widget_button(rect_make(content.x + 176, content.y + 112, 140, 32),
                  "800 x 600", false, false);
    gfx_text(content.x + 24, content.y + 170,
             "Existing windows keep their positions after the change.",
             COLOR_TEXT_DIM);
}

static void panel_draw_appearance(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Appearance", "Apply a desktop wallpaper style immediately");
    widget_button(rect_make(content.x + 24, content.y + 84, 120, 32),
                  "Default", false, false);
    widget_button(rect_make(content.x + 156, content.y + 84, 120, 32),
                  "Solid blue", false, false);
    widget_button(rect_make(content.x + 288, content.y + 84, 120, 32),
                  "Gradient", false, false);
    gfx_text(content.x + 24, content.y + 144,
             "Changes are applied to the desktop immediately.", COLOR_TEXT_DIM);
}

static void panel_draw_sound(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Sound output", "Control the AC97 master output");
    char value[32];
    ksnprintf(value, sizeof(value), "Volume: %u%%", audio_get_volume());
    gfx_text(content.x + 24, content.y + 84, value, COLOR_TEXT);
    widget_button(rect_make(content.x + 24, content.y + 112, 72, 32), "-10", false, false);
    widget_button(rect_make(content.x + 104, content.y + 112, 72, 32), "+10", false, false);
    widget_button(rect_make(content.x + 184, content.y + 112, 120, 32),
                  audio_is_muted() ? "Unmute" : "Mute", false, false);
    gfx_text(content.x + 24, content.y + 170,
             audio_available() ? "Audio adapter available" :
                                 "Audio adapter unavailable", 
             audio_available() ? COLOR_OK : COLOR_DANGER);
}

static void panel_draw_keyboard(control_panel_t *panel, rect_t content)
{
    panel_draw_header(content, "Keyboard layout", "Select the active PS/2 keymap");
    bool spanish = keyboard_get_layout() == KEYBOARD_LAYOUT_ES;
    widget_button(rect_make(content.x + 24, content.y + 84, 120, 32),
                  "US", !spanish, false);
    widget_button(rect_make(content.x + 156, content.y + 84, 120, 32),
                  "Spanish", spanish, false);
    gfx_text(content.x + 24, content.y + 144,
             spanish ? "Spanish layout active" : "US layout active", COLOR_TEXT_DIM);
}

static void panel_draw(window_t *win, rect_t client)
{
    control_panel_t *panel = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    panel_draw_sidebar(panel, client);
    rect_t content = panel_content_rect(client);
    gfx_fill_rect(content.x, content.y, content.w, content.h, COLOR_WINDOW_BG);
    if (panel->subcategory < 0) {
        panel_draw_mosaic(panel, content);
        return;
    }
    if (panel->category == 0 && panel->subcategory == 0)
        panel_draw_network_status(panel, content);
    else if (panel->category == 0 && panel->subcategory == 1)
        panel_draw_network_ip(panel, content);
    else if (panel->category == 0 && panel->subcategory == 2)
        panel_draw_network_connectivity(panel, content);
    else if (panel->category == 1 && panel->subcategory == 0)
        panel_draw_display_resolution(panel, content);
    else if (panel->category == 1 && panel->subcategory == 1)
        panel_draw_appearance(panel, content);
    else if (panel->category == 2 && panel->subcategory == 0)
        panel_draw_sound(panel, content);
    else if (panel->category == 3 && panel->subcategory == 0)
        panel_draw_keyboard(panel, content);
}

static void panel_select_category(control_panel_t *panel, int category)
{
    panel->category = category;
    panel->subcategory = -1;
    panel->active_field = -1;
    for (int i = 0; i < 4; i++)
        panel->fields[i].active = false;
}

static bool panel_apply_manual(control_panel_t *panel)
{
    uint32_t values[4];
    for (int i = 0; i < 4; i++) {
        if (!net_parse_address(panel->fields[i].buffer, &values[i])) {
            char message[160];
            ksnprintf(message, sizeof(message), "Invalid %s", i == 0 ? "IP address" :
                     i == 1 ? "netmask" : i == 2 ? "gateway" : "DNS");
            panel_status(panel, message);
            return false;
        }
    }
    if (!net_configure_manual(values[0], values[1], values[2], values[3])) {
        panel_status(panel, "Network adapter unavailable");
        return false;
    }
    panel->manual = true;
    panel_status(panel, "Manual network configuration applied");
    return true;
}

static void panel_request_dhcp(control_panel_t *panel)
{
    panel->manual = false;
    panel_status(panel, net_request_dhcp() ? "DHCP lease acquired" :
                 "DHCP request failed");
    panel_sync_network(panel);
}

static void panel_run_tests(control_panel_t *panel)
{
    const net_config_t *config = net_config();
    uint32_t resolved = 0, elapsed = 0;
    if (!net_available() || !config->configured) {
        panel_status(panel, "Network is not configured");
        return;
    }
    bool dns_ok = net_dns_lookup("example.com", &resolved, 2500);
    bool ping_ok = config->gateway && net_ping(config->gateway, 1500, &elapsed);
    char address[24], message[160];
    net_format_address(resolved, address, sizeof(address));
    ksnprintf(message, sizeof(message), "DNS %s (%s); ping %s%s",
              dns_ok ? "OK" : "failed", dns_ok ? address : "-",
              ping_ok ? "OK" : "failed", ping_ok ? "" : "");
    panel_status(panel, message);
}

static bool panel_key(window_t *win, const key_event_t *ev)
{
    control_panel_t *panel = win->priv;
    if (ev->code == KEY_ESCAPE) {
        wm_close(win);
        return true;
    }
    if (panel->active_field >= 0) {
        if (ev->code == KEY_TAB) {
            panel->fields[panel->active_field].active = false;
            panel->active_field = (panel->active_field + 1) % 4;
            panel->fields[panel->active_field].active = true;
            panel->fields[panel->active_field].select_all = false;
            wm_invalidate(win);
            return true;
        }
        if (text_input_key(&panel->fields[panel->active_field], ev)) {
            panel->blink_on = true;
            panel->blink_at = timer_ticks() + 500;
            wm_invalidate(win);
            return true;
        }
    }
    return false;
}

static void panel_focus_field(control_panel_t *panel, int field)
{
    for (int i = 0; i < 4; i++)
        panel->fields[i].active = i == field;
    panel->fields[field].select_all = false;
    panel->active_field = field;
}

static bool panel_mouse(window_t *win, const mouse_event_t *ev, int lx, int ly)
{
    control_panel_t *panel = win->priv;
    rect_t client = wm_client_rect(win);
    if (!(ev->pressed & MOUSE_LEFT))
        return true;

    for (int i = 0; i < (int)ARRAY_SIZE(categories); i++) {
        rect_t category = panel_category_rect(i);
        if (rect_contains(category, lx, ly)) {
            panel_select_category(panel, i);
            wm_invalidate(win);
            return true;
        }
        {
            for (int s = 0; s < (int)categories[i].subcategory_count; s++) {
                rect_t sub = panel_subcategory_rect(i, s);
                if (rect_contains(sub, lx, ly)) {
                    panel->category = i;
                    panel->subcategory = s;
                    panel->active_field = -1;
                    for (int f = 0; f < 4; f++)
                        panel->fields[f].active = false;
                    wm_invalidate(win);
                    return true;
                }
            }
        }
    }

    rect_t content = panel_content_rect(client);
    int mx = lx + client.x;
    int my = ly + client.y;
    if (panel->subcategory < 0) {
        for (size_t i = 0; i < categories[panel->category].subcategory_count; i++) {
            int col = (int)(i % 2), row = (int)(i / 2);
            rect_t card = rect_make(content.x + 24 + col * (PANEL_CARD_W + 14),
                                    content.y + 76 + row * (PANEL_CARD_H + 12),
                                    PANEL_CARD_W, PANEL_CARD_H);
            if (rect_contains(card, mx, my)) {
                panel->subcategory = (int)i;
                wm_invalidate(win);
                return true;
            }
        }
        return true;
    }

    if (panel->category == 0 && panel->subcategory == 1) {
        rect_t auto_button = rect_make(content.x + 24, content.y + 72, 130, 30);
        rect_t manual_button = rect_make(content.x + 164, content.y + 72, 130, 30);
        if (rect_contains(auto_button, mx, my)) {
            panel_request_dhcp(panel);
            wm_invalidate(win);
            return true;
        }
        if (rect_contains(manual_button, mx, my)) {
            panel->manual = true;
            panel_status(panel, "Enter values and apply them");
            wm_invalidate(win);
            return true;
        }
        int y = content.y + 116;
        for (int i = 0; i < 4; i++, y += 42) {
            rect_t field = rect_make(content.x + 24, y, 310, 32);
            if (rect_contains(field, mx, my)) {
                panel_focus_field(panel, i);
                wm_invalidate(win);
                return true;
            }
        }
        if (rect_contains(rect_make(content.x + 24, y + 8, 142, 30), mx, my)) {
            panel_apply_manual(panel);
            wm_invalidate(win);
            return true;
        }
        if (rect_contains(rect_make(content.x + 176, y + 8, 142, 30), mx, my)) {
            panel_request_dhcp(panel);
            wm_invalidate(win);
            return true;
        }
    } else if (panel->category == 0 && panel->subcategory == 2 &&
               rect_contains(rect_make(content.x + 24, content.y + 116, 150, 32), mx, my)) {
        panel_run_tests(panel);
        wm_invalidate(win);
        return true;
    } else if (panel->category == 1 && panel->subcategory == 0) {
        if (rect_contains(rect_make(content.x + 24, content.y + 112, 140, 32), mx, my)) {
            if (gfx_resize(1024, 768)) {
                mouse_set_bounds(gfx_width(), gfx_height());
                panel_status(panel, "Resolution changed to 1024 x 768");
                wm_invalidate_all();
            } else {
                panel_status(panel, "Unable to change resolution");
                wm_invalidate(win);
            }
            return true;
        }
        if (rect_contains(rect_make(content.x + 176, content.y + 112, 140, 32), mx, my)) {
            if (gfx_resize(800, 600)) {
                mouse_set_bounds(gfx_width(), gfx_height());
                panel_status(panel, "Resolution changed to 800 x 600");
                wm_invalidate_all();
            } else {
                panel_status(panel, "Unable to change resolution");
                wm_invalidate(win);
            }
            return true;
        }
    } else if (panel->category == 1 && panel->subcategory == 1) {
        if (rect_contains(rect_make(content.x + 24, content.y + 84, 120, 32), mx, my)) {
            desktop_set_wallpaper(DESKTOP_WALLPAPER_DEFAULT,
                                  COLOR_DESKTOP_TOP, COLOR_DESKTOP_BOTTOM);
            panel_status(panel, "Default wallpaper applied");
        } else if (rect_contains(rect_make(content.x + 156, content.y + 84, 120, 32), mx, my)) {
            desktop_set_wallpaper(DESKTOP_WALLPAPER_SOLID,
                                  RGB(0x24, 0x35, 0x52), RGB(0x24, 0x35, 0x52));
            panel_status(panel, "Solid wallpaper applied");
        } else if (rect_contains(rect_make(content.x + 288, content.y + 84, 120, 32), mx, my)) {
            desktop_set_wallpaper(DESKTOP_WALLPAPER_GRADIENT,
                                  RGB(0x48, 0x2f, 0x70), RGB(0x16, 0x1b, 0x30));
            panel_status(panel, "Gradient wallpaper applied");
        } else {
            return true;
        }
        wm_invalidate(win);
        return true;
    } else if (panel->category == 2 && panel->subcategory == 0) {
        if (rect_contains(rect_make(content.x + 24, content.y + 112, 72, 32), mx, my)) {
            uint8_t volume = audio_get_volume();
            audio_set_volume(volume > 10 ? volume - 10 : 0);
        } else if (rect_contains(rect_make(content.x + 104, content.y + 112, 72, 32), mx, my)) {
            uint8_t volume = audio_get_volume();
            audio_set_volume(volume < 90 ? volume + 10 : 100);
        } else if (rect_contains(rect_make(content.x + 184, content.y + 112, 120, 32), mx, my)) {
            audio_set_muted(!audio_is_muted());
        } else {
            return true;
        }
        wm_invalidate(win);
        return true;
    } else if (panel->category == 3 && panel->subcategory == 0) {
        if (rect_contains(rect_make(content.x + 24, content.y + 84, 120, 32), mx, my)) {
            keyboard_set_layout(KEYBOARD_LAYOUT_US);
        } else if (rect_contains(rect_make(content.x + 156, content.y + 84, 120, 32), mx, my)) {
            keyboard_set_layout(KEYBOARD_LAYOUT_ES);
        } else {
            return true;
        }
        wm_invalidate(win);
        return true;
    }
    return true;
}

static void panel_open(window_t *win, const char *arg)
{
    control_panel_t *panel = kcalloc(1, sizeof(*panel));
    win->priv = panel;
    if (!panel)
        return;
    panel->category = 0;
    panel->subcategory = -1;
    panel->active_field = -1;
    panel->blink_on = true;
    panel->window = win;
    bus_subscribe(BUS_NETWORK_CHANGED, panel_bus_event, panel);
    bus_subscribe(BUS_AUDIO_CHANGED, panel_bus_event, panel);
    bus_subscribe(BUS_KEYBOARD_CHANGED, panel_bus_event, panel);
    bus_subscribe(BUS_DISPLAY_CHANGED, panel_bus_event, panel);
    bus_subscribe(BUS_WALLPAPER_CHANGED, panel_bus_event, panel);
    panel_sync_network(panel);
    panel_status(panel, "Ready");
}

static bool panel_tick(window_t *win)
{
    control_panel_t *panel = win->priv;
    if (!panel || panel->active_field < 0)
        return false;
    uint64_t now = timer_ticks();
    if (now < panel->blink_at)
        return false;
    panel->blink_at = now + 500;
    panel->blink_on = !panel->blink_on;
    return true;
}

static void panel_close(window_t *win)
{
    bus_unsubscribe(panel_bus_event, win->priv);
    kfree(win->priv);
    win->priv = NULL;
}

static const app_class_t control_panel_class = {
    .name = "control-panel", .on_open = panel_open, .on_close = panel_close,
    .on_draw = panel_draw, .on_key = panel_key, .on_mouse = panel_mouse,
    .on_tick = panel_tick,
};

window_t *app_control_panel_open(const char *arg)
{
    return wm_open(&control_panel_class, 760, 520, "Control Panel", arg);
}
