/* Application registry: drives the start menu. */
#include <tinos/ui.h>

#define REGISTRY_CAPACITY 16

static app_entry_t registry[REGISTRY_CAPACITY];
static size_t       registry_count;

const app_entry_t *apps_registry(size_t *count)
{
    *count = registry_count;
    return registry;
}

bool apps_register(const app_entry_t *entry)
{
    if (registry_count >= REGISTRY_CAPACITY)
        return false;
    registry[registry_count++] = *entry;
    return true;
}
