/* Small kernel device/driver model. */
#ifndef TINOS_DRIVER_H
#define TINOS_DRIVER_H

#include <tinos/pci.h>
#include <tinos/types.h>

#define DRIVER_MAX 32
#define DEVICE_MAX 64
#define DEVICE_NAME_SIZE 32

typedef enum {
    DEVICE_BUS_NONE = 0,
    DEVICE_BUS_PCI,
} device_bus_t;

typedef struct kernel_device kernel_device_t;
typedef struct kernel_driver kernel_driver_t;

typedef bool (*driver_match_fn)(const kernel_device_t *device);
typedef bool (*driver_probe_fn)(kernel_device_t *device);
typedef void (*driver_remove_fn)(kernel_device_t *device);

struct kernel_device {
    char name[DEVICE_NAME_SIZE];
    device_bus_t bus;
    void *bus_data;
    kernel_driver_t *driver;
    bool bound;
};

struct kernel_driver {
    const char *name;
    device_bus_t bus;
    driver_match_fn match;
    driver_probe_fn probe;
    driver_remove_fn remove;
    bool registered;
};

void driver_model_init(void);
bool driver_register(kernel_driver_t *driver);
bool driver_unregister(kernel_driver_t *driver);
size_t driver_count(void);
size_t device_count(void);
size_t driver_bind_all(void);

/* Bus implementations publish devices through these functions. */
kernel_device_t *device_register(const char *name, device_bus_t bus,
                                 void *bus_data);
void device_unregister(kernel_device_t *device);
kernel_device_t *device_at(size_t index);

/* PCI bus hook used by pci_scan(). Copies the bus record into model storage. */
kernel_device_t *driver_model_add_pci(const pci_device_t *pci);

#endif /* TINOS_DRIVER_H */
