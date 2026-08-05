/* Driver registry and deterministic device binding. */

#include <tinos/driver.h>
#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>

static kernel_driver_t *drivers[DRIVER_MAX];
static kernel_device_t devices[DEVICE_MAX];
static pci_device_t pci_records[DEVICE_MAX];
static size_t device_total;
static size_t pci_total;

void driver_model_init(void)
{
    memset(drivers, 0, sizeof(drivers));
    memset(devices, 0, sizeof(devices));
    memset(pci_records, 0, sizeof(pci_records));
    device_total = 0;
    pci_total = 0;
}

bool driver_register(kernel_driver_t *driver)
{
    if (!driver || !driver->name || !driver->match || !driver->probe ||
        driver->registered)
        return false;
    for (size_t i = 0; i < ARRAY_SIZE(drivers); i++) {
        if (drivers[i])
            continue;
        drivers[i] = driver;
        driver->registered = true;
        driver_bind_all();
        klog("[drv ] registered %s\n", driver->name);
        return true;
    }
    return false;
}

bool driver_unregister(kernel_driver_t *driver)
{
    if (!driver || !driver->registered)
        return false;
    for (size_t i = 0; i < ARRAY_SIZE(devices); i++) {
        kernel_device_t *device = &devices[i];
        if (device->bound && device->driver == driver) {
            if (driver->remove)
                driver->remove(device);
            device->driver = NULL;
            device->bound = false;
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(drivers); i++)
        if (drivers[i] == driver)
            drivers[i] = NULL;
    driver->registered = false;
    return true;
}

size_t driver_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(drivers); i++)
        if (drivers[i])
            count++;
    return count;
}

size_t device_count(void)
{
    return device_total;
}

kernel_device_t *device_register(const char *name, device_bus_t bus,
                                 void *bus_data)
{
    if (device_total >= ARRAY_SIZE(devices) || !bus_data)
        return NULL;
    kernel_device_t *device = &devices[device_total++];
    memset(device, 0, sizeof(*device));
    ksnprintf(device->name, sizeof(device->name), "%s", name ? name :
              "device");
    device->bus = bus;
    device->bus_data = bus_data;
    return device;
}

void device_unregister(kernel_device_t *device)
{
    if (!device || (!device->bound && !device->bus_data))
        return;
    if (device->bound && device->driver && device->driver->remove)
        device->driver->remove(device);
    device->bound = false;
    device->driver = NULL;
    device->bus_data = NULL;
}

kernel_device_t *device_at(size_t index)
{
    return index < device_total ? &devices[index] : NULL;
}

size_t driver_bind_all(void)
{
    size_t bound = 0;
    for (size_t d = 0; d < device_total; d++) {
        kernel_device_t *device = &devices[d];
        if (device->bound)
            continue;
        for (size_t i = 0; i < ARRAY_SIZE(drivers); i++) {
            kernel_driver_t *driver = drivers[i];
            if (!driver || driver->bus != device->bus ||
                !driver->match(device))
                continue;
            if (!driver->probe(device))
                continue;
            device->driver = driver;
            device->bound = true;
            bound++;
            klog("[drv ] %s bound to %s\n", driver->name, device->name);
            break;
        }
    }
    return bound;
}

kernel_device_t *driver_model_add_pci(const pci_device_t *pci)
{
    if (!pci || pci_total >= ARRAY_SIZE(pci_records))
        return NULL;
    pci_records[pci_total] = *pci;
    pci_device_t *record = &pci_records[pci_total++];
    char name[DEVICE_NAME_SIZE];
    ksnprintf(name, sizeof(name), "pci:%02x:%02x.%u", record->bus,
              record->slot, record->func);
    return device_register(name, DEVICE_BUS_PCI, record);
}
