#include <tinos/bus.h>
#include <tinos/kernel.h>

#define BUS_MAX_LISTENERS 32
#define BUS_QUEUE_SIZE    64

typedef struct {
    bus_event_type_t type;
    bus_listener_fn listener;
    void *arg;
    bool active;
} bus_listener_t;

static bus_listener_t listeners[BUS_MAX_LISTENERS];
static bus_event_t queue[BUS_QUEUE_SIZE];
static size_t queue_head;
static size_t queue_tail;

void bus_init(void)
{
    queue_head = queue_tail = 0;
    for (size_t i = 0; i < ARRAY_SIZE(listeners); i++)
        listeners[i].active = false;
}

bool bus_subscribe(bus_event_type_t type, bus_listener_fn listener, void *arg)
{
    if (!listener)
        return false;
    for (size_t i = 0; i < ARRAY_SIZE(listeners); i++) {
        if (listeners[i].active && listeners[i].type == type &&
            listeners[i].listener == listener && listeners[i].arg == arg)
            return true;
    }
    for (size_t i = 0; i < ARRAY_SIZE(listeners); i++) {
        if (!listeners[i].active) {
            listeners[i] = (bus_listener_t){ type, listener, arg, true };
            return true;
        }
    }
    return false;
}

void bus_unsubscribe(bus_listener_fn listener, void *arg)
{
    for (size_t i = 0; i < ARRAY_SIZE(listeners); i++)
        if (listeners[i].active && listeners[i].listener == listener &&
            listeners[i].arg == arg)
            listeners[i].active = false;
}

bool bus_publish(const bus_event_t *event)
{
    if (!event)
        return false;
    size_t next = (queue_head + 1) % BUS_QUEUE_SIZE;
    if (next == queue_tail)
        return false;
    queue[queue_head] = *event;
    queue_head = next;
    return true;
}

size_t bus_dispatch(size_t budget)
{
    size_t dispatched = 0;
    while (queue_tail != queue_head && dispatched < budget) {
        bus_event_t event = queue[queue_tail];
        queue_tail = (queue_tail + 1) % BUS_QUEUE_SIZE;
        for (size_t i = 0; i < ARRAY_SIZE(listeners); i++)
            if (listeners[i].active && listeners[i].type == event.type)
                listeners[i].listener(&event, listeners[i].arg);
        dispatched++;
    }
    return dispatched;
}
