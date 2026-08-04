/* Graphics: linear framebuffer, double buffering and drawing primitives.
 *
 * All drawing happens into a system-memory back buffer; gfx_present() copies
 * only the rows that changed into the adapter's linear framebuffer.
 */
#ifndef TINOS_GFX_H
#define TINOS_GFX_H

#include <tinos/kernel.h>
#include <tinos/types.h>

typedef uint32_t color_t;

#define RGB(r, g, b)                                                          \
    ((color_t)(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define RGB_R(c) (((c) >> 16) & 0xFF)
#define RGB_G(c) (((c) >> 8) & 0xFF)
#define RGB_B(c) ((c) & 0xFF)

#define FONT_W 8
#define FONT_H 16

typedef struct {
    int x, y, w, h;
} rect_t;

typedef struct {
    uintptr_t phys;    /* linear framebuffer address                */
    uint32_t *back;    /* system memory back buffer                 */
    int       width;
    int       height;
    int       pitch;   /* framebuffer bytes per scanline            */
    int       bpp;
    bool      active;
} fb_info_t;

extern const uint8_t font8x16[256][16];

/* Mode setting ------------------------------------------------------- */
bool             gfx_init(int width, int height);
const fb_info_t *gfx_info(void);
bool             gfx_active(void);
int              gfx_width(void);
int              gfx_height(void);

/* Presentation -------------------------------------------------------- */
void gfx_present(void);
void gfx_mark_dirty(int x, int y, int w, int h);
void gfx_mark_all_dirty(void);

/* Clipping ------------------------------------------------------------ */
void   gfx_clip_push(rect_t r); /* intersected with the current clip */
void   gfx_clip_pop(void);
void   gfx_clip_reset(void);
rect_t gfx_clip_current(void);

/* Primitives ---------------------------------------------------------- */
void gfx_clear(color_t color);
void gfx_pixel(int x, int y, color_t color);
void gfx_fill_rect(int x, int y, int w, int h, color_t color);
void gfx_rect(int x, int y, int w, int h, color_t color);
void gfx_hline(int x, int y, int w, color_t color);
void gfx_vline(int x, int y, int h, color_t color);
void gfx_gradient_v(int x, int y, int w, int h, color_t top, color_t bottom);
void gfx_bevel(int x, int y, int w, int h, color_t light, color_t dark);
void gfx_blend_rect(int x, int y, int w, int h, color_t color, uint8_t alpha);

/* Text ---------------------------------------------------------------- */
void gfx_char(int x, int y, char c, color_t fg);
void gfx_char_bg(int x, int y, char c, color_t fg, color_t bg);
void gfx_text(int x, int y, const char *s, color_t fg);
void gfx_text_bg(int x, int y, const char *s, color_t fg, color_t bg);
void gfx_text_n(int x, int y, const char *s, size_t n, color_t fg);
int  gfx_text_width(const char *s);

/* Rectangle helpers ---------------------------------------------------- */
static inline rect_t rect_make(int x, int y, int w, int h)
{
    rect_t r = { x, y, w, h };
    return r;
}

static inline bool rect_contains(rect_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

rect_t rect_intersect(rect_t a, rect_t b);
bool   rect_empty(rect_t r);

#endif /* TINOS_GFX_H */
