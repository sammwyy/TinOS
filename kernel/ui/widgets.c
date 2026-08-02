/* Reusable interface pieces: icons, buttons, panels, scrollbars and a
 * single line text field.  Everything is drawn with the primitives in
 * gfx.c - there are no bitmaps beyond the font. */

#include <tinos/gfx.h>
#include <tinos/string.h>
#include <tinos/ui.h>

/* ------------------------------------------------------------------ */
/* Shapes                                                              */
/* ------------------------------------------------------------------ */

static void fill_circle(int cx, int cy, int radius, color_t color)
{
    for (int dy = -radius; dy <= radius; dy++) {
        int span = 0;
        while (span * span + dy * dy <= radius * radius)
            span++;
        span--;
        gfx_hline(cx - span, cy + dy, span * 2 + 1, color);
    }
}

static void ring(int cx, int cy, int radius, int thickness, color_t color)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= radius * radius &&
                d2 >= (radius - thickness) * (radius - thickness))
                gfx_pixel(cx + dx, cy + dy, color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Icons                                                               */
/* ------------------------------------------------------------------ */

void widget_icon(int x, int y, int size, icon_t icon)
{
    int s = size;

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

/* ------------------------------------------------------------------ */
/* Panels, buttons, scrollbars                                         */
/* ------------------------------------------------------------------ */

void widget_panel(rect_t r, color_t fill, color_t border)
{
    gfx_fill_rect(r.x, r.y, r.w, r.h, fill);
    gfx_rect(r.x, r.y, r.w, r.h, border);
}

void widget_button(rect_t r, const char *label, bool hot, bool pressed)
{
    color_t face = pressed ? COLOR_ACCENT_DARK
                           : (hot ? COLOR_PANEL_LIGHT : COLOR_PANEL);
    gfx_fill_rect(r.x, r.y, r.w, r.h, face);
    gfx_bevel(r.x, r.y, r.w, r.h, pressed ? COLOR_BORDER : COLOR_PANEL_LIGHT,
              pressed ? COLOR_PANEL_LIGHT : RGB(0x14, 0x18, 0x22));
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
    gfx_fill_rect(track.x, track.y, track.w, track.h, RGB(0x1c, 0x21, 0x2c));
    if (total == 0 || visible >= total)
        return;

    int thumb_h = (int)((uint64_t)track.h * visible / total);
    if (thumb_h < 16)
        thumb_h = 16;
    int span = track.h - thumb_h;
    int pos  = (int)((uint64_t)span * first_visible / (total - visible));
    gfx_fill_rect(track.x + 1, track.y + pos, track.w - 2, thumb_h,
                  COLOR_BORDER);
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
    widget_panel(r, RGB(0x18, 0x1d, 0x28), COLOR_ACCENT);

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
