/* Framebuffer graphics engine.
 *
 * Everything is drawn into a back buffer held in ordinary system memory and
 * copied to the adapter once per frame.  A small set of dirty rectangles keeps
 * unrelated changes from expanding into one large bounding-box upload.
 *
 * A small clip stack lets windows draw with absolute screen coordinates
 * while being confined to their own frame.
 */

#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#include <tinos/vbe.h>

#define CLIP_STACK_DEPTH 16

static fb_info_t fb;
static rect_t    clip_stack[CLIP_STACK_DEPTH];
static int       clip_depth;
static rect_t    clip;
#define MAX_DIRTY_RECTS 32
static rect_t dirty_rects[MAX_DIRTY_RECTS];
static size_t dirty_count;

/* ------------------------------------------------------------------ */
/* Rectangles                                                          */
/* ------------------------------------------------------------------ */

bool rect_empty(rect_t r)
{
    return r.w <= 0 || r.h <= 0;
}

rect_t rect_intersect(rect_t a, rect_t b)
{
    int x0 = MAX(a.x, b.x);
    int y0 = MAX(a.y, b.y);
    int x1 = MIN(a.x + a.w, b.x + b.w);
    int y1 = MIN(a.y + a.h, b.y + b.h);
    return rect_make(x0, y0, MAX(0, x1 - x0), MAX(0, y1 - y0));
}

/* ------------------------------------------------------------------ */
/* Mode setting                                                        */
/* ------------------------------------------------------------------ */

bool gfx_init(int width, int height)
{
    vbe_mode_t mode;

    if (!vbe_probe(&mode))
        return false;
    if (!vbe_set_mode((uint16_t)width, (uint16_t)height, 32))
        return false;

    fb.phys   = mode.lfb_phys;
    fb.width  = width;
    fb.height = height;
    fb.bpp    = 32;
    fb.pitch  = width * 4;
    fb.back   = kmalloc((size_t)width * (size_t)height * 4);
    if (!fb.back) {
        klog("[gfx ] out of memory for the %dx%d back buffer\n", width, height);
        vbe_disable();
        return false;
    }
    fb.active = true;

    gfx_clip_reset();
    memset(fb.back, 0, (size_t)width * (size_t)height * 4);
    gfx_mark_all_dirty();
    gfx_present();
    klog("[gfx ] %dx%dx%d back buffer %p -> lfb 0x%08x\n", width, height,
         fb.bpp, fb.back, (uint32_t)fb.phys);
    return true;
}

const fb_info_t *gfx_info(void)
{
    return &fb;
}

bool gfx_active(void)
{
    return fb.active;
}

int gfx_width(void)
{
    return fb.width;
}

int gfx_height(void)
{
    return fb.height;
}

/* ------------------------------------------------------------------ */
/* Presentation                                                        */
/* ------------------------------------------------------------------ */

void gfx_mark_dirty(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    rect_t r = rect_intersect(rect_make(x, y, w, h),
                              rect_make(0, 0, fb.width, fb.height));
    if (rect_empty(r))
        return;

    for (size_t i = 0; i < dirty_count; i++) {
        rect_t old = dirty_rects[i];
        if (r.x > old.x + old.w || old.x > r.x + r.w ||
            r.y > old.y + old.h || old.y > r.y + r.h)
            continue;
        int x0 = MIN(old.x, r.x);
        int y0 = MIN(old.y, r.y);
        int x1 = MAX(old.x + old.w, r.x + r.w);
        int y1 = MAX(old.y + old.h, r.y + r.h);
        dirty_rects[i] = rect_make(x0, y0, x1 - x0, y1 - y0);
        /* Recheck against earlier rectangles after growing this one. */
        for (size_t j = 0; j < i; j++) {
            rect_t prior = dirty_rects[j];
            if (dirty_rects[i].x > prior.x + prior.w ||
                prior.x > dirty_rects[i].x + dirty_rects[i].w ||
                dirty_rects[i].y > prior.y + prior.h ||
                prior.y > dirty_rects[i].y + dirty_rects[i].h)
                continue;
            int ux0 = MIN(prior.x, dirty_rects[i].x);
            int uy0 = MIN(prior.y, dirty_rects[i].y);
            int ux1 = MAX(prior.x + prior.w,
                          dirty_rects[i].x + dirty_rects[i].w);
            int uy1 = MAX(prior.y + prior.h,
                          dirty_rects[i].y + dirty_rects[i].h);
            dirty_rects[j] = rect_make(ux0, uy0, ux1 - ux0, uy1 - uy0);
            dirty_rects[i] = dirty_rects[--dirty_count];
            gfx_mark_dirty(dirty_rects[j].x, dirty_rects[j].y,
                           dirty_rects[j].w, dirty_rects[j].h);
            return;
        }
        return;
    }
    if (dirty_count < MAX_DIRTY_RECTS) {
        dirty_rects[dirty_count++] = r;
        return;
    }
    /* Too many small regions: one bounded upload is cheaper than bookkeeping. */
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    for (size_t i = 0; i < dirty_count; i++) {
        x0 = MIN(x0, dirty_rects[i].x);
        y0 = MIN(y0, dirty_rects[i].y);
        x1 = MAX(x1, dirty_rects[i].x + dirty_rects[i].w);
        y1 = MAX(y1, dirty_rects[i].y + dirty_rects[i].h);
    }
    dirty_rects[0] = rect_make(x0, y0, x1 - x0, y1 - y0);
    dirty_count = 1;
}

void gfx_mark_all_dirty(void)
{
    dirty_rects[0] = rect_make(0, 0, fb.width, fb.height);
    dirty_count = 1;
}

void gfx_present(void)
{
    if (!fb.active || dirty_count == 0)
        return;

    volatile uint8_t *lfb = (volatile uint8_t *)fb.phys;
    for (size_t i = 0; i < dirty_count; i++) {
        rect_t r = dirty_rects[i];
        for (int y = r.y; y < r.y + r.h; y++) {
            void       *dst = (void *)(lfb + (size_t)y * fb.pitch + r.x * 4);
            const void *src = fb.back + (size_t)y * fb.width + r.x;
            memcpy(dst, src, (size_t)r.w * 4);
        }
    }
    dirty_count = 0;
}

/* ------------------------------------------------------------------ */
/* Clipping                                                            */
/* ------------------------------------------------------------------ */

void gfx_clip_reset(void)
{
    clip_depth = 0;
    clip       = rect_make(0, 0, fb.width, fb.height);
}

void gfx_clip_push(rect_t r)
{
    if (clip_depth >= CLIP_STACK_DEPTH) {
        klog("[gfx ] clip stack overflow\n");
        return;
    }
    clip_stack[clip_depth++] = clip;
    clip                     = rect_intersect(clip, r);
}

void gfx_clip_pop(void)
{
    if (clip_depth <= 0)
        return;
    clip = clip_stack[--clip_depth];
}

rect_t gfx_clip_current(void)
{
    return clip;
}

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

void gfx_pixel(int x, int y, color_t color)
{
    if (!fb.active)
        return;
    if (!rect_contains(clip, x, y))
        return;
    fb.back[(size_t)y * fb.width + x] = color;
    gfx_mark_dirty(x, y, 1, 1);
}

void gfx_fill_rect(int x, int y, int w, int h, color_t color)
{
    if (!fb.active)
        return;
    rect_t r = rect_intersect(rect_make(x, y, w, h), clip);
    if (rect_empty(r))
        return;

    for (int row = r.y; row < r.y + r.h; row++) {
        uint32_t *p = fb.back + (size_t)row * fb.width + r.x;
        for (int col = 0; col < r.w; col++)
            p[col] = color;
    }
    gfx_mark_dirty(r.x, r.y, r.w, r.h);
}

void gfx_clear(color_t color)
{
    gfx_fill_rect(0, 0, fb.width, fb.height, color);
}

void gfx_hline(int x, int y, int w, color_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int x, int y, int h, color_t color)
{
    gfx_fill_rect(x, y, 1, h, color);
}

void gfx_rect(int x, int y, int w, int h, color_t color)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_gradient_v(int x, int y, int w, int h, color_t top, color_t bottom)
{
    if (h <= 0)
        return;
    int r0 = (int)RGB_R(top), g0 = (int)RGB_G(top), b0 = (int)RGB_B(top);
    int r1 = (int)RGB_R(bottom), g1 = (int)RGB_G(bottom), b1 = (int)RGB_B(bottom);

    for (int i = 0; i < h; i++) {
        int r = r0 + (r1 - r0) * i / h;
        int g = g0 + (g1 - g0) * i / h;
        int b = b0 + (b1 - b0) * i / h;
        gfx_hline(x, y + i, w, RGB(r, g, b));
    }
}

void gfx_bevel(int x, int y, int w, int h, color_t light, color_t dark)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_hline(x, y, w, light);
    gfx_vline(x, y, h, light);
    gfx_hline(x, y + h - 1, w, dark);
    gfx_vline(x + w - 1, y, h, dark);
}

void gfx_blend_rect(int x, int y, int w, int h, color_t color, uint8_t alpha)
{
    if (!fb.active)
        return;
    rect_t r = rect_intersect(rect_make(x, y, w, h), clip);
    if (rect_empty(r))
        return;

    uint32_t sr = RGB_R(color), sg = RGB_G(color), sb = RGB_B(color);
    for (int row = r.y; row < r.y + r.h; row++) {
        uint32_t *p = fb.back + (size_t)row * fb.width + r.x;
        for (int col = 0; col < r.w; col++) {
            uint32_t d  = p[col];
            uint32_t dr = RGB_R(d), dg = RGB_G(d), db = RGB_B(d);
            uint32_t nr = (sr * alpha + dr * (255 - alpha)) / 255;
            uint32_t ng = (sg * alpha + dg * (255 - alpha)) / 255;
            uint32_t nb = (sb * alpha + db * (255 - alpha)) / 255;
            p[col]      = RGB(nr, ng, nb);
        }
    }
    gfx_mark_dirty(r.x, r.y, r.w, r.h);
}

void gfx_blit_image(rect_t dst, const uint32_t *pixels, int width, int height)
{
    if (!fb.active || !pixels || width <= 0 || height <= 0 || dst.w <= 0 ||
        dst.h <= 0)
        return;
    rect_t r = rect_intersect(dst, clip);
    if (rect_empty(r))
        return;
    for (int y = r.y; y < r.y + r.h; y++) {
        int sy = (int)(((int64_t)(y - dst.y) * height) / dst.h);
        if (sy >= height)
            sy = height - 1;
        for (int x = r.x; x < r.x + r.w; x++) {
            int sx = (int)(((int64_t)(x - dst.x) * width) / dst.w);
            if (sx >= width)
                sx = width - 1;
            uint32_t source = pixels[(size_t)sy * width + sx];
            uint32_t alpha = source >> 24;
            uint32_t *target = fb.back + (size_t)y * fb.width + x;
            if (alpha == 255) {
                *target = source & 0x00ffffff;
            } else if (alpha) {
                uint32_t dest = *target;
                uint32_t sr = (source >> 16) & 0xff;
                uint32_t sg = (source >> 8) & 0xff;
                uint32_t sb = source & 0xff;
                uint32_t dr = (dest >> 16) & 0xff;
                uint32_t dg = (dest >> 8) & 0xff;
                uint32_t db = dest & 0xff;
                *target = RGB((sr * alpha + dr * (255 - alpha)) / 255,
                              (sg * alpha + dg * (255 - alpha)) / 255,
                              (sb * alpha + db * (255 - alpha)) / 255);
            }
        }
    }
    gfx_mark_dirty(r.x, r.y, r.w, r.h);
}

/* ------------------------------------------------------------------ */
/* Text                                                               */
/* ------------------------------------------------------------------ */

static void draw_glyph(int x, int y, char c, color_t fg, color_t bg,
                       bool opaque)
{
    if (!fb.active)
        return;
    uint8_t code = (uint8_t)c;

    rect_t r = rect_intersect(rect_make(x, y, FONT_W, FONT_H), clip);
    if (rect_empty(r))
        return;

    const uint8_t *glyph = font8x16[code];
    for (int row = r.y; row < r.y + r.h; row++) {
        uint8_t   bits = glyph[row - y];
        uint32_t *p    = fb.back + (size_t)row * fb.width;
        for (int col = r.x; col < r.x + r.w; col++) {
            if (bits & (0x80 >> (col - x)))
                p[col] = fg;
            else if (opaque)
                p[col] = bg;
        }
    }
    gfx_mark_dirty(r.x, r.y, r.w, r.h);
}

void gfx_char(int x, int y, char c, color_t fg)
{
    draw_glyph(x, y, c, fg, 0, false);
}

void gfx_char_bg(int x, int y, char c, color_t fg, color_t bg)
{
    draw_glyph(x, y, c, fg, bg, true);
}

void gfx_text(int x, int y, const char *s, color_t fg)
{
    for (; *s; s++, x += FONT_W)
        draw_glyph(x, y, *s, fg, 0, false);
}

void gfx_text_bg(int x, int y, const char *s, color_t fg, color_t bg)
{
    for (; *s; s++, x += FONT_W)
        draw_glyph(x, y, *s, fg, bg, true);
}

void gfx_text_n(int x, int y, const char *s, size_t n, color_t fg)
{
    for (size_t i = 0; i < n && s[i]; i++, x += FONT_W)
        draw_glyph(x, y, s[i], fg, 0, false);
}

int gfx_text_width(const char *s)
{
    return (int)strlen(s) * FONT_W;
}
