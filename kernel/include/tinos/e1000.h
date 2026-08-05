/* Intel PRO/1000 (QEMU e1000) network adapter. */
#ifndef TINOS_E1000_H
#define TINOS_E1000_H

#include <tinos/types.h>

bool e1000_driver_register(void);
bool e1000_available(void);
const uint8_t *e1000_mac(void);
bool e1000_send(const void *data, size_t length);
size_t e1000_receive(void *data, size_t capacity);
void e1000_poll(void);

#endif /* TINOS_E1000_H */
