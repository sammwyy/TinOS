#include <tinos/string.h>
#include <tinos/widgets.h>

extern const uint8_t font8x16[256][16];

static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }

static void layout(tinos_widget_view_t *view, unsigned parent, unsigned depth)
{
    if (depth >= TINOS_WIDGET_LIMIT)
        return;
    tinos_widget_t *container = &view->nodes[parent];
    if (container->kind != TINOS_WIDGET_COLUMN &&
        container->kind != TINOS_WIDGET_ROW &&
        container->kind != TINOS_WIDGET_PANEL)
        return;
    const int padding = container->kind == TINOS_WIDGET_PANEL ? 10 : 8;
    const int gap = 7;
    unsigned children = 0, flex_total = 0;
    int min_horizontal = 0, min_vertical = 0;
    for (unsigned i = parent + 1; i < view->count; i++) {
        tinos_widget_t *node = &view->nodes[i];
        if (node->parent != parent)
            continue;
        children++;
        min_horizontal += node->min_width;
        min_vertical += node->min_height;
        flex_total += node->flex;
    }
    if (!children)
        return;
    bool horizontal = container->kind == TINOS_WIDGET_ROW &&
                      container->width - padding * 2 >=
                          min_horizontal + (int)(children - 1) * gap;
    int available = (horizontal ? container->width : container->height) -
                    padding * 2 - (int)(children - 1) * gap;
    int free_space = maximum(0, available -
                            (horizontal ? min_horizontal : min_vertical));
    int cursor = (horizontal ? container->x : container->y) + padding;
    for (unsigned i = parent + 1; i < view->count; i++) {
        tinos_widget_t *node = &view->nodes[i];
        if (node->parent != parent)
            continue;
        int size = (horizontal ? node->min_width : node->min_height) +
                   (flex_total ? free_space * node->flex / flex_total : 0);
        if (horizontal) {
            node->x = cursor;
            node->y = container->y + padding;
            node->width = maximum(0, size);
            node->height = maximum(0, container->height - 2 * padding);
        } else {
            node->x = container->x + padding;
            node->y = cursor;
            node->width = maximum(0, container->width - 2 * padding);
            node->height = maximum(0, size);
        }
        cursor += size + gap;
        layout(view, i, depth + 1);
    }
}

static void fill(tinos_widget_view_t *view, int tx, int ty, int tw, int th,
                 int x, int y, int w, int h, uint32_t color)
{
    int left = maximum(x, tx), top = maximum(y, ty);
    int right = minimum(x + w, tx + tw);
    int bottom = minimum(y + h, ty + th);
    if (right <= left || bottom <= top)
        return;
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++)
            view->tile[(py - ty) * tw + px - tx] = color;
}

static void text(tinos_widget_view_t *view, int tx, int ty, int tw, int th,
                 int x, int y, int max_width, const char *value, uint32_t color)
{
    if (!value || max_width < 8)
        return;
    int right = minimum((int)view->width, x + max_width);
    for (unsigned n = 0; n < 96 && value[n] && x + 8 <= right; n++, x += 8) {
        const uint8_t *glyph = font8x16[(uint8_t)value[n]];
        for (int row = 0; row < 16; row++) {
            int py = y + row;
            if (py < ty || py >= ty + th || py < 0 ||
                py >= (int)view->height)
                continue;
            for (int col = 0; col < 8; col++) {
                int px = x + col;
                if ((glyph[row] & (0x80u >> col)) &&
                    px >= tx && px < tx + tw && px >= 0)
                    view->tile[(py - ty) * tw + px - tx] = color;
            }
        }
    }
}

static void paint(tinos_widget_view_t *view, int tx, int ty, int tw, int th)
{
    const tinos_gui_theme_t *theme = &view->theme;
    fill(view, tx, ty, tw, th, tx, ty, tw, th, theme->background);
    for (unsigned i = 1; i < view->count; i++) {
        tinos_widget_t *node = &view->nodes[i];
        int x = node->x, y = node->y, w = node->width, h = node->height;
        if (w <= 0 || h <= 0 || x >= tx + tw || y >= ty + th ||
            x + w <= tx || y + h <= ty)
            continue;
        switch (node->kind) {
        case TINOS_WIDGET_BANNER:
            fill(view, tx, ty, tw, th, x, y, w, h, theme->accent);
            text(view, tx, ty, tw, th, x + 12, y + (h - 16) / 2,
                 maximum(0, w - 24), node->text, theme->accent_text);
            break;
        case TINOS_WIDGET_PANEL:
            fill(view, tx, ty, tw, th, x, y, w, h, theme->border);
            fill(view, tx, ty, tw, th, x + 1, y + 1,
                 maximum(0, w - 2), maximum(0, h - 2), theme->surface);
            break;
        case TINOS_WIDGET_BUTTON:
        case TINOS_WIDGET_FIELD: {
            bool focused = view->focused == (int)i;
            uint32_t background = node->kind == TINOS_WIDGET_BUTTON && focused
                                      ? theme->accent : theme->surface;
            fill(view, tx, ty, tw, th, x, y, w, h, theme->border);
            fill(view, tx, ty, tw, th, x + 1, y + 1,
                 maximum(0, w - 2), maximum(0, h - 2), background);
            const char *label = node->kind == TINOS_WIDGET_FIELD ?
                                (node->input ? node->input : "") : node->text;
            text(view, tx, ty, tw, th, x + 8, y + (h - 16) / 2,
                 maximum(0, w - 16),
                 label, focused && node->kind == TINOS_WIDGET_BUTTON ?
                        theme->accent_text : theme->foreground);
            if (focused && node->kind == TINOS_WIDGET_FIELD && node->input) {
                int caret = x + 8 +
                    minimum((int)strlen(label), maximum(0, (w - 18) / 8)) * 8;
                fill(view, tx, ty, tw, th, caret, y + h - 5, 7, 1,
                     theme->accent);
            }
            break;
        }
        case TINOS_WIDGET_LABEL:
            text(view, tx, ty, tw, th, x + 2, y + (h - 16) / 2,
                 maximum(0, w - 4), node->text, theme->foreground);
            break;
        case TINOS_WIDGET_LIST: {
            fill(view, tx, ty, tw, th, x, y, w, h, theme->border);
            fill(view, tx, ty, tw, th, x + 1, y + 1,
                 maximum(0, w - 2), maximum(0, h - 2), theme->surface);
            unsigned visible = (unsigned)maximum(0, (h - 8) / 20);
            if (!visible)
                break;
            if (node->selected < node->scroll)
                node->scroll = node->selected;
            if (node->selected >= node->scroll + visible)
                node->scroll = node->selected - visible + 1;
            for (unsigned row = 0; row < visible; row++) {
                unsigned entry = node->scroll + row;
                if (!node->items || entry >= node->item_count)
                    break;
                int iy = y + 4 + (int)row * 20;
                if (entry == node->selected)
                    fill(view, tx, ty, tw, th, x + 2, iy, w - 4, 20,
                         theme->selection);
                text(view, tx, ty, tw, th, x + 9, iy + 2,
                     maximum(0, w - 18), node->items[entry],
                     entry == node->selected ? theme->accent_text :
                                                theme->foreground);
            }
            break;
        }
        default:
            break;
        }
    }
    if (view->on_paint) {
        tinos_canvas_t canvas = { view, tx, ty, tw, th };
        view->on_paint(view, &canvas, view->context);
    }
}

void tinos_canvas_fill(tinos_canvas_t *canvas, int x, int y, int width,
                       int height, uint32_t color)
{
    if (canvas && width > 0 && height > 0)
        fill(canvas->view, canvas->tx, canvas->ty, canvas->width,
             canvas->height, x, y, width, height, color);
}

void tinos_canvas_frame(tinos_canvas_t *canvas, int x, int y, int width,
                        int height, uint32_t color)
{
    tinos_canvas_fill(canvas, x, y, width, 1, color);
    tinos_canvas_fill(canvas, x, y + height - 1, width, 1, color);
    tinos_canvas_fill(canvas, x, y, 1, height, color);
    tinos_canvas_fill(canvas, x + width - 1, y, 1, height, color);
}

void tinos_canvas_text(tinos_canvas_t *canvas, int x, int y, int max_width,
                       const char *value, uint32_t color)
{
    if (canvas)
        text(canvas->view, canvas->tx, canvas->ty, canvas->width,
             canvas->height, x, y, max_width, value, color);
}

void tinos_canvas_gradient(tinos_canvas_t *canvas, int x, int y, int width,
                           int height, uint32_t top, uint32_t bottom)
{
    if (!canvas || height <= 0)
        return;
    int start = maximum(y, canvas->ty);
    int end = minimum(y + height, canvas->ty + canvas->height);
    for (int py = start; py < end; py++) {
        int t = py - y, d = maximum(1, height - 1);
        uint32_t color = 0;
        for (int shift = 0; shift <= 16; shift += 8) {
            uint32_t a = (top >> shift) & 255u;
            uint32_t b = (bottom >> shift) & 255u;
            color |= ((a * (d - t) + b * t) / (uint32_t)d) << shift;
        }
        tinos_canvas_fill(canvas, x, py, width, 1, color);
    }
}

void tinos_canvas_button(tinos_canvas_t *canvas, int x, int y, int width,
                         int height, const char *label, bool active)
{
    if (!canvas || width < 4 || height < 4)
        return;
    const tinos_gui_theme_t *theme = &canvas->view->theme;
    tinos_canvas_fill(canvas, x, y, width, height,
                      active ? theme->panel_light : theme->panel);
    tinos_canvas_fill(canvas, x, y, width, 1, theme->panel_light);
    tinos_canvas_fill(canvas, x, y, 1, height, theme->panel_light);
    tinos_canvas_fill(canvas, x, y + height - 1, width, 1, theme->border);
    tinos_canvas_fill(canvas, x + width - 1, y, 1, height, theme->border);
    tinos_canvas_frame(canvas, x, y, width, height,
                       active ? theme->accent_light : theme->border);
    if (label) {
        int length = minimum((int)strlen(label), (width - 8) / 8);
        int left = x + (width - length * 8) / 2;
        tinos_canvas_text(canvas, left, y + (height - 16) / 2,
                          width - (left - x), label, theme->foreground);
    }
}

void tinos_canvas_image(tinos_canvas_t *canvas, int x, int y, int width,
                        int height, const uint32_t *pixels,
                        unsigned source_width, unsigned source_height)
{
    if (!canvas || !pixels || width <= 0 || height <= 0 ||
        !source_width || !source_height)
        return;
    int left = maximum(maximum(x, canvas->tx), 0);
    int top = maximum(maximum(y, canvas->ty), 0);
    int right = minimum(minimum(x + width, canvas->tx + canvas->width),
                        (int)canvas->view->width);
    int bottom = minimum(minimum(y + height, canvas->ty + canvas->height),
                         (int)canvas->view->height);
    for (int py = top; py < bottom; py++)
        for (int px = left; px < right; px++) {
            unsigned sx = (unsigned)(px - x) * source_width / (unsigned)width;
            unsigned sy = (unsigned)(py - y) * source_height / (unsigned)height;
            uint32_t color = pixels[sy * source_width + sx];
            unsigned alpha = color >> 24;
            if (!alpha)
                continue;
            uint32_t *target = &canvas->view->tile[(py - canvas->ty) *
                                                   canvas->width + px - canvas->tx];
            if (alpha == 255) {
                *target = color & 0x00ffffffu;
            } else {
                uint32_t previous = *target, blended = 0;
                for (unsigned shift = 0; shift <= 16; shift += 8) {
                    unsigned a = (color >> shift) & 255u;
                    unsigned b = (previous >> shift) & 255u;
                    blended |= ((a * alpha + b * (255 - alpha)) / 255u) << shift;
                }
                *target = blended;
            }
        }
}

int tinos_widget_open(tinos_widget_view_t *view, const char *title,
                      unsigned width, unsigned height)
{
    if (!view || tinos_gui_theme(&view->theme) < 0)
        return -1;
    view->handle = -1;
    unsigned configured_width = 0, configured_height = 0;
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        int handle = tinos_gui_open(title, width, height);
        if (handle >= 0) {
            tinos_gui_size_t size;
            if (tinos_gui_surface_size(handle, &size) == 0 &&
                tinos_gui_surface_config(handle,
                    minimum(size.width, TINOS_GUI_SURFACE_MAX_WIDTH),
                    minimum(size.height, TINOS_GUI_SURFACE_MAX_HEIGHT)) == 0) {
                view->handle = handle;
                configured_width = minimum(size.width,
                                           TINOS_GUI_SURFACE_MAX_WIDTH);
                configured_height = minimum(size.height,
                                            TINOS_GUI_SURFACE_MAX_HEIGHT);
                break;
            }
            tinos_gui_close(handle);
        }
        width = maximum(200, width * 3 / 4);
        height = maximum(160, height * 3 / 4);
    }
    if (view->handle < 0)
        return -1;
    view->width = configured_width;
    view->height = configured_height;
    view->count = 1;
    view->focused = -1;
    view->dirty = true;
    view->nodes[0] = (tinos_widget_t){ .kind = TINOS_WIDGET_COLUMN };
    return view->handle;
}

int tinos_widget_add(tinos_widget_view_t *view, unsigned parent,
                     tinos_widget_kind_t kind, unsigned id, const char *label,
                     unsigned min_width, unsigned min_height, unsigned flex)
{
    if (!view || parent >= view->count || view->count == TINOS_WIDGET_LIMIT ||
        id > UINT16_MAX || kind > TINOS_WIDGET_BANNER ||
        min_width > UINT16_MAX || min_height > UINT16_MAX ||
        flex > UINT16_MAX)
        return -1;
    unsigned index = view->count++;
    view->nodes[index] = (tinos_widget_t){
        .kind = kind, .id = id, .parent = parent, .text = label,
        .min_width = min_width, .min_height = min_height, .flex = flex
    };
    view->dirty = true;
    return index;
}

int tinos_widget_field(tinos_widget_view_t *view, unsigned parent, unsigned id,
                       char *value, unsigned capacity, unsigned flex)
{
    if (!value || capacity < 2 || capacity > UINT16_MAX)
        return -1;
    int index = tinos_widget_add(view, parent, TINOS_WIDGET_FIELD, id, NULL,
                                 100, 32, flex);
    if (index >= 0) {
        view->nodes[index].input = value;
        view->nodes[index].input_capacity = capacity;
        if (view->focused < 0)
            view->focused = index;
    }
    return index;
}

int tinos_widget_list(tinos_widget_view_t *view, unsigned parent, unsigned id,
                      unsigned min_height, unsigned flex)
{
    int index = tinos_widget_add(view, parent, TINOS_WIDGET_LIST, id, NULL,
                                 120, min_height, flex);
    if (index >= 0 && view->focused < 0)
        view->focused = index;
    return index;
}

void tinos_widget_list_items(tinos_widget_view_t *view, unsigned index,
                             const char *const *items, unsigned count,
                             unsigned selected)
{
    if (!view || index >= view->count ||
        view->nodes[index].kind != TINOS_WIDGET_LIST)
        return;
    tinos_widget_t *node = &view->nodes[index];
    node->items = items;
    node->item_count = items ? count : 0;
    node->selected = selected < node->item_count ? selected : 0;
    node->scroll = 0;
    view->dirty = true;
}

int tinos_widget_flush(tinos_widget_view_t *view)
{
    if (!view || view->handle < 1)
        return -1;
    tinos_gui_size_t size;
    if (tinos_gui_surface_size(view->handle, &size) < 0)
        return -1;
    unsigned width = minimum(size.width, TINOS_GUI_SURFACE_MAX_WIDTH);
    unsigned height = minimum(size.height, TINOS_GUI_SURFACE_MAX_HEIGHT);
    if (!width || !height)
        return -1;
    if (view->width != width || view->height != height) {
        if (tinos_gui_surface_config(view->handle, width, height) == 0) {
            view->width = width;
            view->height = height;
        } else if (!view->width || !view->height) {
            return -1;
        }
        /* An existing surface remains usable if a larger allocation fails. */
        view->dirty = true;
    }
    width = view->width;
    height = view->height;
    if (!view->dirty)
        return 0;
    view->nodes[0].x = view->nodes[0].y = 0;
    view->nodes[0].width = width;
    view->nodes[0].height = height;
    layout(view, 0, 0);
    for (unsigned y = 0; y < height; y += TINOS_GUI_TILE_MAX_HEIGHT)
        for (unsigned x = 0; x < width; x += TINOS_GUI_TILE_MAX_WIDTH) {
            unsigned tw = minimum(width - x, TINOS_GUI_TILE_MAX_WIDTH);
            unsigned th = minimum(height - y, TINOS_GUI_TILE_MAX_HEIGHT);
            paint(view, x, y, tw, th);
            if (tinos_gui_surface_tile(view->handle, x, y, tw, th,
                                       view->tile) < 0)
                return -1;
        }
    view->dirty = false;
    return 0;
}

int tinos_widget_event(tinos_widget_view_t *view, const tinos_gui_event_t *event)
{
    if (!view || !event)
        return -1;
    if (event->type == TINOS_GUI_RESIZE) {
        view->dirty = true;
        return 1;
    }
    if (event->type == TINOS_GUI_MOUSE && (event->code & 1u)) {
        for (int i = (int)view->count - 1; i > 0; i--) {
            tinos_widget_t *node = &view->nodes[i];
            if ((node->kind == TINOS_WIDGET_BUTTON ||
                 node->kind == TINOS_WIDGET_FIELD ||
                 node->kind == TINOS_WIDGET_LIST) &&
                event->x >= node->x && event->y >= node->y &&
                event->x - node->x < node->width &&
                event->y - node->y < node->height) {
                view->focused = i;
                view->dirty = true;
                if (node->kind == TINOS_WIDGET_LIST) {
                    unsigned row = (unsigned)maximum(0, event->y -
                                                     node->y - 4) / 20u;
                    unsigned selected = node->scroll + row;
                    if (selected < node->item_count)
                        node->selected = selected;
                    return 1;
                }
                if (node->kind == TINOS_WIDGET_BUTTON && view->on_action)
                    view->on_action(view, node->id, view->context);
                return 1;
            }
        }
    }
    if (event->type != TINOS_GUI_KEY)
        return 0;
    if (event->code == TINOS_KEY_TAB) {
        for (unsigned i = 1; i <= view->count; i++) {
            unsigned index = (view->focused + i) % view->count;
            if (view->nodes[index].kind == TINOS_WIDGET_FIELD ||
                view->nodes[index].kind == TINOS_WIDGET_BUTTON) {
                view->focused = index;
                view->dirty = true;
                return 1;
            }
        }
    }
    if (view->focused <= 0 || view->focused >= (int)view->count)
        return 0;
    tinos_widget_t *node = &view->nodes[view->focused];
    if (node->kind == TINOS_WIDGET_LIST) {
        if (event->code == TINOS_KEY_UP && node->selected) {
            node->selected--;
            view->dirty = true;
            return 1;
        }
        if (event->code == TINOS_KEY_DOWN &&
            node->selected + 1 < node->item_count) {
            node->selected++;
            view->dirty = true;
            return 1;
        }
    }
    if (event->code == TINOS_KEY_ENTER) {
        if (view->on_action &&
            (node->kind == TINOS_WIDGET_FIELD ||
             node->kind == TINOS_WIDGET_BUTTON ||
             node->kind == TINOS_WIDGET_LIST))
            view->on_action(view, node->id, view->context);
        view->dirty = true;
        return 1;
    }
    if (node->kind != TINOS_WIDGET_FIELD || !node->input)
        return 0;
    size_t length = strlen(node->input);
    if (event->code == TINOS_KEY_BACKSPACE && length) {
        node->input[length - 1] = '\0';
    } else if (event->ch >= 32 && event->ch <= 126 &&
               length + 1 < node->input_capacity) {
        node->input[length] = (char)event->ch;
        node->input[length + 1] = '\0';
    } else {
        return 0;
    }
    view->dirty = true;
    return 1;
}

void tinos_widget_close(tinos_widget_view_t *view)
{
    if (view && view->handle > 0) {
        tinos_gui_close(view->handle);
        view->handle = 0;
    }
}

void tinos_widget_theme(tinos_widget_view_t *view,
                        const tinos_gui_theme_t *theme)
{
    if (view && theme) {
        view->theme = *theme;
        view->dirty = true;
    }
}

void tinos_signal_init(tinos_signal_t *signal, uint32_t value)
{
    if (signal) {
        memset(signal, 0, sizeof(*signal));
        signal->value = value;
    }
}

int tinos_signal_watch(tinos_signal_t *signal, tinos_widget_view_t *view)
{
    if (!signal || !view)
        return -1;
    for (unsigned i = 0; i < 4; i++)
        if (signal->watchers[i] == view)
            return 0;
    for (unsigned i = 0; i < 4; i++)
        if (!signal->watchers[i]) {
            signal->watchers[i] = view;
            return 0;
        }
    return -1;
}

void tinos_signal_unwatch(tinos_signal_t *signal, tinos_widget_view_t *view)
{
    if (!signal)
        return;
    for (unsigned i = 0; i < 4; i++)
        if (signal->watchers[i] == view)
            signal->watchers[i] = NULL;
}

void tinos_signal_set(tinos_signal_t *signal, uint32_t value)
{
    if (!signal || signal->value == value)
        return;
    signal->value = value;
    signal->revision++;
    for (unsigned i = 0; i < 4; i++)
        if (signal->watchers[i])
            signal->watchers[i]->dirty = true;
}

uint32_t tinos_signal_get(const tinos_signal_t *signal)
{
    return signal ? signal->value : 0;
}
