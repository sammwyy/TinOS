/* Small in-kernel event bus for cross-subsystem notifications. */
#ifndef TINOS_BUS_H
#define TINOS_BUS_H

#include <tinos/types.h>

typedef enum {
    BUS_NETWORK_CHANGED,
    BUS_AUDIO_CHANGED,
    BUS_KEYBOARD_CHANGED,
    BUS_DISPLAY_CHANGED,
    BUS_WALLPAPER_CHANGED,
} bus_event_type_t;

typedef struct {
    bus_event_type_t type;
    uint32_t value1;
    uint32_t value2;
    uint32_t value3;
} bus_event_t;

typedef void (*bus_listener_fn)(const bus_event_t *event, void *arg);

void bus_init(void);
bool bus_subscribe(bus_event_type_t type, bus_listener_fn listener, void *arg);
void bus_unsubscribe(bus_listener_fn listener, void *arg);
bool bus_publish(const bus_event_t *event);
size_t bus_dispatch(size_t budget);

#endif
