/* Userspace executable metadata for the start menu and desktop.
 *
 * Every /apps/gui/<app>.elf is an application. An optional sibling
 * /apps/gui/<app>.app manifest supplies its display name, menu category,
 * icon and summary as `key=value` lines; without one the name is derived
 * from the file name. Entries are kept sorted by category, then name. */
#include <tinos/fs.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define REGISTRY_CAPACITY 16
#define GUI_DIR "/apps/gui/"

typedef struct {
    char name[32];
    char category[16];
    char icon[20];
    char summary[48];
    char path[sizeof(GUI_DIR) + FS_NAME_MAX];
} registry_text_t;

static app_entry_t     registry[REGISTRY_CAPACITY];
static registry_text_t registry_text[REGISTRY_CAPACITY];
static size_t          registry_count;

static const char *const category_order[] = {
    "Accessories", "Multimedia", "System",
};

const app_entry_t *apps_registry(size_t *count)
{
    *count = registry_count;
    return registry;
}

const app_entry_t *apps_find_path(const char *path)
{
    for (size_t i = 0; path && i < registry_count; i++)
        if (strcmp(registry[i].path, path) == 0)
            return &registry[i];
    return NULL;
}

static void copy_value(char *out, size_t size, const char *value,
                       size_t length)
{
    while (length && (value[length - 1] == '\r' || value[length - 1] == ' '))
        length--;
    size_t n = MIN(length, size - 1);
    memcpy(out, value, n);
    out[n] = '\0';
}

static void parse_manifest(registry_text_t *text, app_entry_t *entry,
                           const fs_node_t *file)
{
    const char *data = (const char *)file->data;
    size_t size = file->size;
    for (size_t at = 0; at < size;) {
        size_t end = at;
        while (end < size && data[end] != '\n')
            end++;
        const char *line = data + at;
        size_t length = end - at;
        const char *eq = NULL;
        for (size_t i = 0; i < length && !eq; i++)
            if (line[i] == '=')
                eq = line + i;
        if (eq && line[0] != '#') {
            size_t key = (size_t)(eq - line);
            const char *value = eq + 1;
            size_t vlen = length - key - 1;
            if (key == 4 && memcmp(line, "name", 4) == 0)
                copy_value(text->name, sizeof(text->name), value, vlen);
            else if (key == 8 && memcmp(line, "category", 8) == 0)
                copy_value(text->category, sizeof(text->category), value, vlen);
            else if (key == 4 && memcmp(line, "icon", 4) == 0)
                copy_value(text->icon, sizeof(text->icon), value, vlen);
            else if (key == 7 && memcmp(line, "summary", 7) == 0)
                copy_value(text->summary, sizeof(text->summary), value, vlen);
            else if (key == 7 && memcmp(line, "desktop", 7) == 0)
                entry->desktop = vlen && value[0] == '1';
        }
        at = end + 1;
    }
}

static int category_rank(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(category_order); i++)
        if (strcmp(category_order[i], name) == 0)
            return (int)i;
    return (int)ARRAY_SIZE(category_order);
}

static int entry_compare(const app_entry_t *a, const app_entry_t *b)
{
    int ra = category_rank(a->category), rb = category_rank(b->category);
    if (ra != rb)
        return ra - rb;
    int c = strcmp(a->category, b->category);
    return c ? c : strcmp(a->name, b->name);
}

static void bind_text(size_t slot)
{
    registry_text_t *text = &registry_text[slot];
    app_entry_t *entry = &registry[slot];
    entry->name = text->name;
    entry->category = text->category;
    entry->summary = text->summary;
    entry->path = text->path;
    entry->icon_name = text->icon;
}

void apps_discover(void)
{
    registry_count = 0;
    fs_node_t *dir;
    if (fs_resolve(fs_root(), GUI_DIR, &dir) != FS_OK || dir->type != FS_DIR)
        return;
    for (fs_node_t *file = dir->children;
         file && registry_count < REGISTRY_CAPACITY; file = file->next) {
        size_t length = strlen(file->name);
        if (file->type != FS_FILE || !file->data || length <= 4 ||
            strcmp(file->name + length - 4, ".elf") != 0)
            continue;
        size_t slot = registry_count++;
        registry_text_t *text = &registry_text[slot];
        app_entry_t *entry = &registry[slot];
        memset(text, 0, sizeof(*text));
        *entry = (app_entry_t){ .icon = ICON_APP };
        ksnprintf(text->path, sizeof(text->path), GUI_DIR "%s", file->name);

        /* Fallback name: "image_viewer.elf" -> "Image viewer". */
        copy_value(text->name, sizeof(text->name), file->name, length - 4);
        for (char *c = text->name; *c; c++)
            if (*c == '_')
                *c = ' ';
        if (text->name[0] >= 'a' && text->name[0] <= 'z')
            text->name[0] = (char)(text->name[0] - 'a' + 'A');
        strlcpy(text->category, "Programs", sizeof(text->category));
        strlcpy(text->icon, "app", sizeof(text->icon));

        char manifest[FS_NAME_MAX + 8];
        ksnprintf(manifest, sizeof(manifest), "%.*s.app", (int)(length - 4),
                  file->name);
        for (fs_node_t *m = dir->children; m; m = m->next)
            if (m->type == FS_FILE && m->data && strcmp(m->name, manifest) == 0)
                parse_manifest(text, entry, m);
    }

    /* Insertion sort; text buffers move with their entries. */
    for (size_t i = 1; i < registry_count; i++)
        for (size_t j = i; j > 0; j--) {
            bind_text(j);
            bind_text(j - 1);
            if (entry_compare(&registry[j - 1], &registry[j]) <= 0)
                break;
            app_entry_t entry = registry[j];
            registry[j] = registry[j - 1];
            registry[j - 1] = entry;
            registry_text_t text = registry_text[j];
            registry_text[j] = registry_text[j - 1];
            registry_text[j - 1] = text;
        }
    for (size_t i = 0; i < registry_count; i++)
        bind_text(i);
}

void app_icon_draw(int x, int y, int size, const app_entry_t *app)
{
    if (app->custom_icon.pixels)
        gfx_blit_image(rect_make(x, y, size, size), app->custom_icon.pixels,
                       (int)app->custom_icon.width,
                       (int)app->custom_icon.height);
    else if (!app->icon_name || !widget_icon_name(x, y, size, app->icon_name))
        widget_icon(x, y, size, app->icon);
}
