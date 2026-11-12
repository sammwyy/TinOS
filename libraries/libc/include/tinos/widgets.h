#ifndef TINOS_LIBC_WIDGETS_H
#define TINOS_LIBC_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>
#include <tinos/libc.h>

#define TINOS_WIDGET_LIMIT 24u

typedef struct tinos_widget_view tinos_widget_view_t;
typedef struct tinos_canvas {
    tinos_widget_view_t *view;
    int tx, ty, width, height;
} tinos_canvas_t;

typedef struct {
    uint32_t value;
    uint32_t revision;
    tinos_widget_view_t *watchers[4];
} tinos_signal_t;

typedef enum {
    TINOS_WIDGET_COLUMN,
    TINOS_WIDGET_ROW,
    TINOS_WIDGET_PANEL,
    TINOS_WIDGET_LABEL,
    TINOS_WIDGET_BUTTON,
    TINOS_WIDGET_FIELD,
    TINOS_WIDGET_LIST,
    TINOS_WIDGET_BANNER,
} tinos_widget_kind_t;

typedef struct {
    tinos_widget_kind_t kind;
    uint16_t id, parent;
    uint16_t min_width, min_height, flex;
    const char *text;
    char *input;
    uint16_t input_capacity;
    const char *const *items;
    unsigned item_count, selected, scroll;
    int x, y, width, height;
} tinos_widget_t;

struct tinos_widget_view {
    int handle;
    unsigned width, height;
    unsigned count;
    int focused;
    bool dirty;
    tinos_gui_theme_t theme;
    tinos_widget_t nodes[TINOS_WIDGET_LIMIT];
    void (*on_paint)(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                     void *context);
    void (*on_action)(tinos_widget_view_t *view, unsigned id, void *context);
    void *context;
    uint32_t tile[TINOS_GUI_TILE_MAX_WIDTH * TINOS_GUI_TILE_MAX_HEIGHT];
};

int tinos_widget_open(tinos_widget_view_t *view, const char *title,
                      unsigned width, unsigned height);
int tinos_widget_add(tinos_widget_view_t *view, unsigned parent,
                     tinos_widget_kind_t kind, unsigned id, const char *text,
                     unsigned min_width, unsigned min_height, unsigned flex);
int tinos_widget_field(tinos_widget_view_t *view, unsigned parent, unsigned id,
                       char *text, unsigned capacity, unsigned flex);
int tinos_widget_list(tinos_widget_view_t *view, unsigned parent, unsigned id,
                      unsigned min_height, unsigned flex);
void tinos_widget_list_items(tinos_widget_view_t *view, unsigned index,
                             const char *const *items, unsigned count,
                             unsigned selected);
int tinos_widget_flush(tinos_widget_view_t *view);
int tinos_widget_event(tinos_widget_view_t *view, const tinos_gui_event_t *event);
void tinos_widget_theme(tinos_widget_view_t *view,
                        const tinos_gui_theme_t *theme);
void tinos_widget_close(tinos_widget_view_t *view);
void tinos_canvas_fill(tinos_canvas_t *canvas, int x, int y, int width,
                       int height, uint32_t color);
void tinos_canvas_frame(tinos_canvas_t *canvas, int x, int y, int width,
                        int height, uint32_t color);
void tinos_canvas_gradient(tinos_canvas_t *canvas, int x, int y, int width,
                           int height, uint32_t top, uint32_t bottom);
void tinos_canvas_text(tinos_canvas_t *canvas, int x, int y, int max_width,
                       const char *value, uint32_t color);
void tinos_canvas_button(tinos_canvas_t *canvas, int x, int y, int width,
                         int height, const char *label, bool active);
void tinos_canvas_icon(tinos_canvas_t *canvas, const char *name,
                       int x, int y, unsigned size);
void tinos_canvas_image(tinos_canvas_t *canvas, int x, int y, int width,
                        int height, const uint32_t *pixels,
                        unsigned source_width, unsigned source_height);
void tinos_signal_init(tinos_signal_t *signal, uint32_t value);
int tinos_signal_watch(tinos_signal_t *signal, tinos_widget_view_t *view);
void tinos_signal_unwatch(tinos_signal_t *signal, tinos_widget_view_t *view);
void tinos_signal_set(tinos_signal_t *signal, uint32_t value);
uint32_t tinos_signal_get(const tinos_signal_t *signal);

#endif
