/* USB Mass Storage Bulk-Only Transport over an external USB transport. */
#ifndef TINOS_USB_MSC_H
#define TINOS_USB_MSC_H

#include <tinos/block.h>

typedef bool (*usb_msc_bulk_fn)(void *context, void *buffer, size_t length);

typedef struct {
    void          *context;
    usb_msc_bulk_fn bulk_in;
    usb_msc_bulk_fn bulk_out;
    bool (*reset)(void *context);
    uint8_t  lun;
    uint32_t block_size;
    uint32_t block_count;
} usb_msc_transport_t;

/* Initialize a BOT disk after USB enumeration and READ CAPACITY. */
bool usb_msc_init(block_device_t *device, usb_msc_transport_t *transport,
                  const char *name);

/* Issue TEST UNIT READY and READ CAPACITY, then initialize the block device. */
bool usb_msc_probe(block_device_t *device, usb_msc_transport_t *transport,
                   const char *name);

#endif /* TINOS_USB_MSC_H */
