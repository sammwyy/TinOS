/* Reusable interface pieces: icons, buttons, panels, scrollbars and a
 * single line text field.  Everything is drawn with the primitives in
 * gfx.c; icons come from the generated resources/icons atlas. */

#include <tinos/gfx.h>
#include <tinos/string.h>
#include <tinos/ui.h>
#include "icons_generated.h"

/* Icons */

/* The atlas stores each icon at a few hand-tuned sizes. Pick the variant
 * that looks best at `size`: an exact match, a slightly smaller one drawn
 * centred (crisper than resampling), a larger one box-filtered down, or as
 * a last resort the largest one scaled up. *drawn is the edge length that
 * ends up on screen. */
static const custom_icon_t *icon_pick(const char *name, int size, int *drawn)
{
    size_t first = custom_icon_count;
    for (size_t i = 0; i < custom_icon_count; i++)
        if (strcmp(custom_icons[i].name, name) == 0) {
            first = i;
            break;
        }
    if (first == custom_icon_count)
        return NULL;
    size_t last = first;
    while (last + 1 < custom_icon_count &&
           strcmp(custom_icons[last + 1].name, name) == 0)
        last++;

    const custom_icon_t *below = NULL, *above = NULL;
    for (size_t i = first; i <= last; i++) {
        const custom_icon_t *v = &custom_icons[i];
        if (v->size == size) {
            *drawn = size;
            return v;
        }
        if (v->size < size)
            below = v;
        else if (!above)
            above = v;
    }
    if (below && size - below->size <= MAX(2, size / 8)) {
        *drawn = below->size;
        return below;
    }
    *drawn = size;
    return above ? above : &custom_icons[last];
}

static uint32_t icon_texel(const custom_icon_t *v, int x, int y)
{
    return custom_icon_palette[custom_icon_atlas[
        (size_t)(v->y + y) * custom_icon_atlas_width + v->x + x]];
}

/* Non-premultiplied ARGB of output pixel (px, py) of `v` drawn at `size`. */
static uint32_t icon_sample(const custom_icon_t *v, int size, int px, int py)
{
    int vs = v->size;
    if (vs == size)
        return icon_texel(v, px, py);
    if (vs < size)
        return icon_texel(v, px * vs / size, py * vs / size);
    int x0 = px * vs / size, x1 = MAX(x0 + 1, (px + 1) * vs / size);
    int y0 = py * vs / size, y1 = MAX(y0 + 1, (py + 1) * vs / size);
    uint32_t a = 0, r = 0, g = 0, b = 0, n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++, n++) {
            uint32_t t = icon_texel(v, x, y);
            uint32_t ta = t >> 24;
            a += ta;
            r += ((t >> 16) & 0xff) * ta;
            g += ((t >> 8) & 0xff) * ta;
            b += (t & 0xff) * ta;
        }
    if (!a)
        return 0;
    return ((a / n) << 24) | ((r / a) << 16) | ((g / a) << 8) | (b / a);
}

bool icon_render(const char *name, int size, uint32_t *out)
{
    int drawn;
    const custom_icon_t *v;
    if (!name || size <= 0 || !(v = icon_pick(name, size, &drawn)))
        return false;
    int offset = (size - drawn) / 2;
    memset(out, 0, (size_t)size * size * sizeof(uint32_t));
    for (int y = 0; y < drawn; y++)
        for (int x = 0; x < drawn; x++)
            out[(size_t)(y + offset) * size + x + offset] =
                icon_sample(v, drawn, x, y);
    return true;
}

bool widget_icon_name(int x, int y, int size, const char *name)
{
    int drawn;
    const custom_icon_t *v;
    if (!name || size <= 0 || !gfx_active() ||
        !(v = icon_pick(name, size, &drawn)))
        return false;
    x += (size - drawn) / 2;
    y += (size - drawn) / 2;
    const fb_info_t *fb = gfx_info();
    rect_t area = rect_intersect(rect_make(x, y, drawn, drawn),
                                 gfx_clip_current());
    if (rect_empty(area))
        return true;
    for (int py = area.y; py < area.y + area.h; py++) {
        uint32_t *row = fb->back + (size_t)py * fb->width;
        for (int px = area.x; px < area.x + area.w; px++) {
            uint32_t s = icon_sample(v, drawn, px - x, py - y);
            uint32_t alpha = s >> 24;
            if (alpha == 255) {
                row[px] = s & 0x00ffffffu;
            } else if (alpha) {
                uint32_t d = row[px];
                row[px] = RGB((RGB_R(s) * alpha + RGB_R(d) * (255 - alpha)) / 255,
                              (RGB_G(s) * alpha + RGB_G(d) * (255 - alpha)) / 255,
                              (RGB_B(s) * alpha + RGB_B(d) * (255 - alpha)) / 255);
            }
        }
    }
    gfx_mark_dirty(area.x, area.y, area.w, area.h);
    return true;
}

const char *widget_icon_resource(icon_t icon)
{
    static const char *const names[ICON_COUNT] = {
        [ICON_TERMINAL] = "terminal",
        [ICON_FOLDER] = "folder",
        [ICON_FILE] = "file",
        [ICON_INFO] = "info",
        [ICON_CHIP] = "chip",
        [ICON_POWER] = "power",
        [ICON_RESTART] = "restart",
        [ICON_EDITOR] = "editor",
        [ICON_TINOS] = "tinos",
        [ICON_BACK] = "back",
        [ICON_FORWARD] = "forward",
        [ICON_UP] = "up",
        [ICON_HOME] = "home",
        [ICON_DRIVE] = "drive",
        [ICON_REFRESH] = "refresh",
        [ICON_SAVE] = "save",
        [ICON_NEW] = "new",
        [ICON_UNDO] = "undo",
        [ICON_REDO] = "redo",
        [ICON_PLAY] = "play",
        [ICON_PAUSE] = "pause",
        [ICON_VOLUME] = "volume",
        [ICON_VOLUME_MUTE] = "volume_mute",
        [ICON_NETWORK] = "network",
        [ICON_NETWORK_OFF] = "network_off",
        [ICON_MEMORY] = "memory",
        [ICON_SETTINGS] = "settings",
        [ICON_TASKMGR] = "taskmgr",
        [ICON_COMPUTER] = "computer",
        [ICON_APP] = "app",
        [ICON_MENU_FOLDER] = "menu_folder",
        [ICON_MUSIC] = "music",
        [ICON_IMAGE] = "image",
        [ICON_CALCULATOR] = "calculator",
        [ICON_CALENDAR] = "calendar",
        [ICON_CLOCK] = "clock",
        [ICON_DESKTOP] = "desktop",
        [ICON_LOGOUT] = "logout",
        [ICON_USER] = "user",
        [ICON_WARNING] = "warning",
        [ICON_ERROR] = "error",
    };
    return icon >= 0 && icon < ICON_COUNT ? names[icon] : NULL;
}

void widget_icon(int x, int y, int size, icon_t icon)
{
    const char *name = widget_icon_resource(icon);
    if (name)
        (void)widget_icon_name(x, y, size, name);
}

/* Panels, buttons, scrollbars */

void widget_panel(rect_t r, color_t fill, color_t border)
{
    gfx_fill_rect(r.x, r.y, r.w, r.h, fill);
    gfx_bevel(r.x, r.y, r.w, r.h, COLOR_PANEL_LIGHT, COLOR_BORDER);
    gfx_rect(r.x, r.y, r.w, r.h, border);
}

void widget_input_frame(rect_t r)
{
    gfx_fill_rect(r.x, r.y, r.w, r.h, RGB(0xf4, 0xf3, 0xf6));
    gfx_bevel(r.x, r.y, r.w, r.h, COLOR_BORDER, COLOR_PANEL_LIGHT);
    gfx_rect(r.x, r.y, r.w, r.h, COLOR_BORDER);
}

void widget_button(rect_t r, const char *label, bool hot, bool pressed)
{
    color_t face = pressed ? COLOR_ACCENT_DARK
                           : (hot ? COLOR_PANEL_LIGHT : COLOR_PANEL);
    gfx_fill_rect(r.x, r.y, r.w, r.h, face);
    gfx_bevel(r.x, r.y, r.w, r.h, pressed ? COLOR_BORDER : COLOR_PANEL_LIGHT,
              pressed ? COLOR_PANEL_LIGHT : COLOR_BORDER);
    gfx_rect(r.x, r.y, r.w, r.h, hot ? COLOR_ACCENT : COLOR_BORDER);
    widget_text_centered(r, label, COLOR_TEXT);
}

void widget_button_icon(rect_t r, icon_t icon, const char *label, bool hot,
                        bool pressed)
{
    widget_button(r, "", hot, pressed);
    if (!label || !label[0]) {
        widget_icon(r.x + (r.w - 16) / 2, r.y + (r.h - 16) / 2, 16, icon);
        return;
    }
    widget_icon(r.x + 6, r.y + (r.h - 16) / 2, 16, icon);
    widget_text_centered(rect_make(r.x + 26, r.y, r.w - 28, r.h), label,
                         COLOR_TEXT);
}

void widget_slider(rect_t r, int value, int maximum)
{
    if (maximum < 1)
        maximum = 1;
    value = CLAMP(value, 0, maximum);
    gfx_fill_rect(r.x, r.y + r.h / 2 - 3, r.w, 6, RGB(0x35, 0x3e, 0x4d));
    gfx_rect(r.x, r.y + r.h / 2 - 3, r.w, 6, COLOR_BORDER);
    int thumb_x = r.x + (int)((int64_t)(r.w - 12) * value / maximum);
    gfx_fill_rect(thumb_x, r.y, 12, r.h, COLOR_ACCENT);
    gfx_bevel(thumb_x, r.y, 12, r.h, COLOR_PANEL_LIGHT, COLOR_BORDER);
}

int widget_slider_value(rect_t r, int x, int maximum)
{
    if (maximum < 1)
        maximum = 1;
    return CLAMP((x - r.x) * maximum / MAX(1, r.w), 0, maximum);
}

void widget_text_centered(rect_t r, const char *text, color_t color)
{
    int tw = gfx_text_width(text);
    gfx_clip_push(r);
    gfx_text(r.x + (r.w - tw) / 2, r.y + (r.h - FONT_H) / 2, text, color);
    gfx_clip_pop();
}

void widget_scrollbar(rect_t track, size_t first_visible, size_t visible,
                      size_t total)
{
    gfx_fill_rect(track.x, track.y, track.w, track.h, COLOR_PANEL);
    gfx_bevel(track.x, track.y, track.w, track.h, COLOR_BORDER,
              COLOR_PANEL_LIGHT);
    if (total == 0 || visible >= total)
        return;

    int thumb_h = (int)((uint64_t)track.h * visible / total);
    if (thumb_h < 16)
        thumb_h = 16;
    int span = track.h - thumb_h;
    int pos  = (int)((uint64_t)span * first_visible / (total - visible));
    gfx_fill_rect(track.x + 2, track.y + pos, track.w - 4, thumb_h,
                  COLOR_PANEL_LIGHT);
    gfx_bevel(track.x + 2, track.y + pos, track.w - 4, thumb_h,
              COLOR_PANEL_LIGHT, COLOR_BORDER);
}

/* Text input */

void text_input_reset(text_input_t *input, const char *initial)
{
    strlcpy(input->buffer, initial ? initial : "", sizeof(input->buffer));
    input->length     = strlen(input->buffer);
    input->cursor     = input->length;
    input->active     = true;
    input->select_all = input->length > 0;
}

static void clear_selection(text_input_t *input)
{
    input->buffer[0]  = '\0';
    input->length     = 0;
    input->cursor     = 0;
    input->select_all = false;
}

bool text_input_key(text_input_t *input, const key_event_t *ev)
{
    /* Any edit or cursor movement drops the initial selection. */
    if (input->select_all) {
        switch (ev->code) {
        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_HOME:
        case KEY_END:
            input->select_all = false;
            break;
        case KEY_BACKSPACE:
        case KEY_DELETE:
            clear_selection(input);
            return true;
        default:
            if (ev->ch >= 0x20 && ev->ch < 0x7F)
                clear_selection(input);
            break;
        }
    }

    switch (ev->code) {
    case KEY_LEFT:
        if (input->cursor > 0)
            input->cursor--;
        return true;
    case KEY_RIGHT:
        if (input->cursor < input->length)
            input->cursor++;
        return true;
    case KEY_HOME:
        input->cursor = 0;
        return true;
    case KEY_END:
        input->cursor = input->length;
        return true;
    case KEY_BACKSPACE:
        if (input->cursor > 0) {
            memmove(input->buffer + input->cursor - 1,
                    input->buffer + input->cursor,
                    input->length - input->cursor + 1);
            input->cursor--;
            input->length--;
        }
        return true;
    case KEY_DELETE:
        if (input->cursor < input->length) {
            memmove(input->buffer + input->cursor,
                    input->buffer + input->cursor + 1,
                    input->length - input->cursor);
            input->length--;
        }
        return true;
    default:
        break;
    }

    if (ev->ch >= 0x20 && ev->ch < 0x7F &&
        input->length + 1 < sizeof(input->buffer)) {
        memmove(input->buffer + input->cursor + 1,
                input->buffer + input->cursor,
                input->length - input->cursor + 1);
        input->buffer[input->cursor++] = ev->ch;
        input->length++;
        return true;
    }
    return false;
}

void text_input_draw(const text_input_t *input, rect_t r, const char *label,
                     bool blink_on)
{
    widget_input_frame(r);
    gfx_rect(r.x, r.y, r.w, r.h, COLOR_ACCENT);

    int x = r.x + 6;
    int y = r.y + (r.h - FONT_H) / 2;

    if (label) {
        gfx_text(x, y, label, COLOR_TEXT_DIM);
        x += gfx_text_width(label) + 6;
    }

    gfx_clip_push(rect_make(x, r.y, r.x + r.w - x - 4, r.h));
    if (input->select_all && input->length) {
        gfx_fill_rect(x, y, (int)input->length * FONT_W, FONT_H,
                      COLOR_SELECTION);
    }
    gfx_text(x, y, input->buffer, COLOR_TEXT);
    if (blink_on)
        gfx_fill_rect(x + (int)input->cursor * FONT_W, y, 2, FONT_H,
                      COLOR_ACCENT);
    gfx_clip_pop();
}
