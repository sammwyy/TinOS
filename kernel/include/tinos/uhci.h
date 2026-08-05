/* Minimal UHCI USB 1.1 host controller and mass-storage enumerator. */
#ifndef TINOS_UHCI_H
#define TINOS_UHCI_H

#include <tinos/block.h>

size_t uhci_usb_init(void);
size_t uhci_usb_count(void);
block_device_t *uhci_usb_device(size_t index);

#endif /* TINOS_UHCI_H */
