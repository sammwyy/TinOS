#include <tinos/string.h>
#include <tinos/widgets.h>

/* Icons live once, in the kernel's shared atlas; each process keeps a small
 * cache of the few it is currently drawing. A window repaints in 64x64
 * tiles, so one icon is typically drawn several times per frame. When the
 * arena fills up it is simply emptied and refilled on demand. */
#define ICON_ARENA_WORDS (16u * 1024u / 4u)
#define ICON_SLOTS 24u

typedef struct {
    char name[24];
    unsigned size, offset;
    bool missing;
} icon_slot_t;

static uint32_t arena[ICON_ARENA_WORDS];
static unsigned arena_used;
static icon_slot_t slots[ICON_SLOTS];
static unsigned slot_count;

static const icon_slot_t *icon_lookup(const char *name, unsigned size)
{
    for (unsigned i = 0; i < slot_count; i++)
        if (slots[i].size == size && strcmp(slots[i].name, name) == 0)
            return &slots[i];
    if (strlen(name) >= sizeof(slots[0].name))
        return NULL;
    unsigned words = size * size;
    if (words > ICON_ARENA_WORDS)
        return NULL;
    if (slot_count == ICON_SLOTS || arena_used + words > ICON_ARENA_WORDS)
        slot_count = arena_used = 0;
    icon_slot_t *slot = &slots[slot_count++];
    tinos_strlcpy(slot->name, name, sizeof(slot->name));
    slot->size = size;
    slot->offset = arena_used;
    slot->missing = tinos_icon(name, size, arena + arena_used) != 0;
    if (!slot->missing)
        arena_used += words;
    return slot;
}

void tinos_canvas_icon(tinos_canvas_t *canvas, const char *name,
                       int x, int y, unsigned size)
{
    if (!canvas || !name || size < 1 || size > TINOS_ICON_MAX)
        return;
    if (x >= canvas->tx + canvas->width ||
        y >= canvas->ty + canvas->height ||
        x + (int)size <= canvas->tx ||
        y + (int)size <= canvas->ty)
        return;
    const icon_slot_t *slot = icon_lookup(name, size);
    if (!slot || slot->missing)
        return;
    const uint32_t *pixels = arena + slot->offset;
    tinos_widget_view_t *view = canvas->view;
    int top = y < canvas->ty ? canvas->ty - y : 0;
    int left = x < canvas->tx ? canvas->tx - x : 0;
    int bottom = y + (int)size > canvas->ty + canvas->height ?
                 canvas->ty + canvas->height - y : (int)size;
    int right = x + (int)size > canvas->tx + canvas->width ?
                canvas->tx + canvas->width - x : (int)size;
    for (int py = top; py < bottom; py++) {
        uint32_t *row = view->tile + (y + py - canvas->ty) * canvas->width +
                        x - canvas->tx;
        for (int px = left; px < right; px++) {
            uint32_t s = pixels[py * (int)size + px];
            uint32_t a = s >> 24;
            if (a == 255) {
                row[px] = s & 0x00ffffffu;
            } else if (a) {
                uint32_t d = row[px];
                uint32_t r = (((s >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
                uint32_t g = (((s >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
                uint32_t b = ((s & 255) * a + (d & 255) * (255 - a)) / 255;
                row[px] = (r << 16) | (g << 8) | b;
            }
        }
    }
}
