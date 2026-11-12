#ifndef TINOS_UAPI_TLS_H
#define TINOS_UAPI_TLS_H

#define TINOS_TLS_MODULES 8u
#define TINOS_TLS_TCB_BYTES 96u
#define TINOS_TLS_BASE_WORD(slot) (1u + (slot))
#define TINOS_TLS_SIZE_WORD(slot) (1u + TINOS_TLS_MODULES + (slot))

#endif
