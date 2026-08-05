/* Reusable interface pieces: icons, buttons, panels, scrollbars and a
 * single line text field.  Everything is drawn with the primitives in
 * gfx.c - there are no bitmaps beyond the font. */

#include <tinos/gfx.h>
#include <tinos/string.h>
#include <tinos/ui.h>
#include "icons_generated.h"

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Icons                                                               */
/* ------------------------------------------------------------------ */

#if 0 /* Legacy hardcoded icon table; resources/icons is authoritative. */
typedef struct {
    icon_t   icon;
    uint16_t mask[16];
} builtin_icon_t;

/* One-bit 16x16 sprites. Zeroes are transparent, so the same small raster
 * assets work on the lavender shell, light panels and dark terminal. */
static const builtin_icon_t builtin_icons[] = {
    { ICON_FOLDER, { 0x0000, 0x0000, 0x1f00, 0x3f80, 0x7fff, 0x7fff,
                     0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff, 0x7fff,
                     0x7fff, 0x7fff, 0x7fff, 0x0000 } },
    { ICON_FILE, { 0x0ff0, 0x1ff8, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc,
                   0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc,
                   0x3ffc, 0x3ffc, 0x3ffc, 0x0000 } },
    { ICON_DRIVE, { 0x0000, 0x0ff0, 0x1ff8, 0x3ffc, 0x3ffc, 0x3ffc,
                    0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc,
                    0x3ffc, 0x3ffc, 0x1ff8, 0x0000 } },
    { ICON_TERMINAL, { 0x7ffe, 0x4002, 0x4802, 0x5402, 0x6202, 0x4002,
                       0x4002, 0x4002, 0x4002, 0x4002, 0x4002, 0x4002,
                       0x4002, 0x4002, 0x7ffe, 0x0000 } },
};

static color_t builtin_icon_color(icon_t icon)
{
    switch (icon) {
    case ICON_FOLDER:
        return COLOR_FOLDER;
    case ICON_FILE:
        return COLOR_TEXT_DIM;
    case ICON_HOME:
        return COLOR_ACCENT_DARK;
    case ICON_DRIVE:
        return COLOR_ACCENT_DARK;
    case ICON_TERMINAL:
        return COLOR_TERMINAL_TEXT;
    default:
        return COLOR_TEXT;
    }
}

static bool draw_builtin_icon(int x, int y, int size, icon_t icon)
{
    const builtin_icon_t *sprite = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(builtin_icons); i++)
        if (builtin_icons[i].icon == icon) {
            sprite = &builtin_icons[i];
            break;
        }
    if (!sprite)
        return false;

    color_t color = builtin_icon_color(icon);
    for (int py = 0; py < size; py++) {
        int sy = py * 16 / size;
        for (int px = 0; px < size; px++) {
            int sx = px * 16 / size;
            if (sprite->mask[sy] & (uint16_t)(0x8000u >> sx))
                gfx_pixel(x + px, y + py, color);
        }
    }
    return true;
}
#endif

bool widget_icon_name(int x, int y, int size, const char *name)
{
    if (!name || size <= 0)
        return false;
    const custom_icon_t *icon = NULL;
    for (size_t i = 0; i < custom_icon_count; i++) {
        if (strcmp(custom_icons[i].name, name) == 0) {
            icon = &custom_icons[i];
            break;
        }
    }
    if (!icon)
        return false;

    for (int py = 0; py < size; py++) {
        int sy = py * icon->height / size;
        for (int px = 0; px < size; px++) {
            int sx = px * icon->width / size;
            uint32_t pixel = custom_icon_atlas[(size_t)(icon->y + sy) *
                                               custom_icon_atlas_width +
                                               icon->x + sx];
            if ((pixel >> 24) != 0)
                gfx_pixel(x + px, y + py, pixel & 0x00ffffffu);
        }
    }
    return true;
}

#if 0
void widget_icon(int x, int y, int size, icon_t icon)
{
    int s = size;

    if (draw_builtin_icon(x, y, size, icon))
        return;

    switch (icon) {
    case ICON_TERMINAL:
        gfx_fill_rect(x, y, s, s, RGB(0x11, 0x16, 0x20));
        gfx_rect(x, y, s, s, RGB(0x4a, 0x5a, 0x76));
        gfx_hline(x + 1, y + 1, s - 2, RGB(0x2a, 0x35, 0x48));
        gfx_hline(x + 1, y + 2, s - 2, RGB(0x2a, 0x35, 0x48));
        gfx_text(x + 3, y + (s - FONT_H) / 2 + 2, ">", COLOR_TERMINAL_PROMPT);
        gfx_fill_rect(x + 11, y + s - 7, 6, 2, COLOR_TERMINAL_TEXT);
        break;

    case ICON_FOLDER:
        gfx_fill_rect(x, y + 3, s / 2, 3, RGB(0xd8, 0xa8, 0x3c));
        gfx_fill_rect(x, y + 5, s, s - 8, COLOR_FOLDER);
        gfx_hline(x, y + 5, s, RGB(0xff, 0xdd, 0x8a));
        gfx_rect(x, y + 5, s, s - 8, RGB(0xb8, 0x8a, 0x22));
        break;

    case ICON_FILE:
    case ICON_EDITOR:
        gfx_fill_rect(x + 2, y + 1, s - 5, s - 2, COLOR_FILE);
        gfx_rect(x + 2, y + 1, s - 5, s - 2, RGB(0x8a, 0x93, 0xa4));
        for (int i = 0; i < 4; i++)
            gfx_hline(x + 5, y + 5 + i * 3, s - 11, RGB(0x8a, 0x93, 0xa4));
        if (icon == ICON_EDITOR) {
            /* a little pencil across the corner */
            for (int i = 0; i < 7; i++)
                gfx_pixel(x + s - 8 + i, y + 9 - i, RGB(0xf0, 0x9a, 0x3c));
        }
        break;

    case ICON_INFO:
        fill_circle(x + s / 2, y + s / 2, s / 2 - 1, COLOR_ACCENT);
        gfx_fill_rect(x + s / 2 - 1, y + s / 2 - 5, 2, 2, COLOR_TEXT);
        gfx_fill_rect(x + s / 2 - 1, y + s / 2 - 1, 2, 6, COLOR_TEXT);
        break;

    case ICON_CHIP:
        gfx_fill_rect(x + 3, y + 3, s - 6, s - 6, RGB(0x35, 0x74, 0x9a));
        gfx_rect(x + 3, y + 3, s - 6, s - 6, RGB(0x9a, 0xd4, 0xf0));
        gfx_fill_rect(x + 6, y + 6, s - 12, s - 12, RGB(0x14, 0x2a, 0x3c));
        for (int i = 0; i < 3; i++) {
            gfx_hline(x, y + 6 + i * 4, 3, RGB(0x9a, 0xd4, 0xf0));
            gfx_hline(x + s - 3, y + 6 + i * 4, 3, RGB(0x9a, 0xd4, 0xf0));
            gfx_vline(x + 6 + i * 4, y, 3, RGB(0x9a, 0xd4, 0xf0));
            gfx_vline(x + 6 + i * 4, y + s - 3, 3, RGB(0x9a, 0xd4, 0xf0));
        }
        break;

    case ICON_POWER:
        ring(x + s / 2, y + s / 2 + 1, s / 2 - 2, 2, COLOR_DANGER);
        gfx_fill_rect(x + s / 2 - 1, y + 1, 2, s / 2 - 1, COLOR_DANGER);
        gfx_fill_rect(x + s / 2 - 3, y + 1, 6, 2, COLOR_WINDOW_BG);
        break;

    case ICON_RESTART:
        ring(x + s / 2, y + s / 2, s / 2 - 2, 2, COLOR_OK);
        gfx_fill_rect(x + s / 2, y + 1, s / 2, 5, COLOR_PANEL);
        for (int i = 0; i < 5; i++)
            gfx_hline(x + s / 2 + 1, y + 2 + i, 5 - i, COLOR_OK);
        break;

    case ICON_BACK:
    case ICON_FORWARD:
    case ICON_UP: {
        color_t c = COLOR_ACCENT_DARK;
        int cy = y + s / 2;
        if (icon == ICON_BACK || icon == ICON_FORWARD) {
            int dir = icon == ICON_BACK ? -1 : 1;
            gfx_hline(x + 4, cy, s - 8, c);
            gfx_hline(x + (dir < 0 ? 3 : s - 4), cy, 1, c);
            for (int i = 0; i < 5; i++) {
                gfx_pixel(x + (dir < 0 ? 4 + i : s - 5 - i), cy - i, c);
                gfx_pixel(x + (dir < 0 ? 4 + i : s - 5 - i), cy + i, c);
            }
        } else {
            gfx_vline(x + s / 2, y + 4, s - 8, c);
            for (int i = 0; i < 5; i++) {
                gfx_pixel(x + s / 2 - i, y + 4 + i, c);
                gfx_pixel(x + s / 2 + i, y + 4 + i, c);
            }
        }
        break;
    }

    case ICON_HOME:
        gfx_fill_rect(x + 4, y + 8, s - 8, s - 5, RGB(0xf3, 0xf2, 0xf5));
        gfx_rect(x + 4, y + 8, s - 8, s - 5, COLOR_BORDER);
        for (int i = 0; i < s / 2; i++) {
            gfx_pixel(x + s / 2 - i, y + 7 - i, RGB(0xb8, 0x4f, 0x58));
            gfx_pixel(x + s / 2 + i, y + 7 - i, RGB(0xb8, 0x4f, 0x58));
        }
        gfx_fill_rect(x + s / 2 - 2, y + s - 7, 4, 5, COLOR_ACCENT_DARK);
        break;

    case ICON_REFRESH:
        ring(x + s / 2, y + s / 2, s / 2 - 3, 2, COLOR_ACCENT_DARK);
        gfx_fill_rect(x + s / 2 + 2, y + 2, 5, 5, COLOR_PANEL);
        gfx_hline(x + s - 7, y + 3, 4, COLOR_ACCENT_DARK);
        gfx_vline(x + s - 4, y + 3, 4, COLOR_ACCENT_DARK);
        break;

    case ICON_SAVE:
        gfx_fill_rect(x + 3, y + 2, s - 6, s - 4, COLOR_ACCENT_DARK);
        gfx_rect(x + 3, y + 2, s - 6, s - 4, COLOR_BORDER);
        gfx_fill_rect(x + 6, y + 3, s - 12, 5, COLOR_PANEL_LIGHT);
        gfx_fill_rect(x + 6, y + s - 8, s - 12, 5, COLOR_PANEL_LIGHT);
        break;

    case ICON_NEW:
        gfx_fill_rect(x + 3, y + 2, s - 6, s - 4, COLOR_FILE);
        gfx_rect(x + 3, y + 2, s - 6, s - 4, COLOR_BORDER);
        gfx_hline(x + 6, y + 6, s - 12, COLOR_BORDER);
        gfx_hline(x + 6, y + 10, s - 12, COLOR_BORDER);
        break;

    case ICON_UNDO:
    case ICON_REDO:
        gfx_hline(x + 4, y + s / 2, s - 8, COLOR_ACCENT_DARK);
        gfx_vline(icon == ICON_UNDO ? x + 4 : x + s - 5, y + 5, s - 10,
                  COLOR_ACCENT_DARK);
        gfx_hline(x + 4, y + 5, s - 9, COLOR_ACCENT_DARK);
        break;

    case ICON_TINOS:
    default: {
        /* Four pointed star: the half width of each row falls off
         * quadratically, which gives the concave sides of a spark. */
        int r  = s / 2;
        int cx = x + r;
        int cy = y + r;
        for (int dy = -r; dy <= r; dy++) {
            int d  = r - (dy < 0 ? -dy : dy);
            int hw = d * d / (r ? r : 1);
            if (hw <= 0)
                continue;
            color_t shade = (dy > -r / 3 && dy < r / 3)
                                ? RGB(0xdc, 0xea, 0xff)
                                : COLOR_ACCENT;
            gfx_hline(cx - hw, cy + dy, hw * 2 + 1, shade);
        }
        break;
    }
    }
}
#endif

static const char *icon_resource_name(icon_t icon)
{
    static const char *const names[] = {
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
    };
    return icon >= 0 && (size_t)icon < ARRAY_SIZE(names) ? names[icon] : NULL;
}

void widget_icon(int x, int y, int size, icon_t icon)
{
    const char *name = icon_resource_name(icon);
    if (name)
        (void)widget_icon_name(x, y, size, name);
}

/* ------------------------------------------------------------------ */
/* Panels, buttons, scrollbars                                         */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Text input                                                          */
/* ------------------------------------------------------------------ */

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
