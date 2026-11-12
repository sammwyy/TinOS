#ifndef TINOS_UAPI_SETTINGS_H
#define TINOS_UAPI_SETTINGS_H

#include <stdint.h>

/* IPv4 addresses use network-order bit significance, as in net_config_t. */
typedef struct {
    uint32_t address, netmask, gateway, dns;
    uint32_t configured, available;
} tinos_net_settings_t;

typedef struct {
    uint32_t volume, muted;
} tinos_audio_settings_t;

typedef struct {
    char host[64];
    uint32_t address;
} tinos_dns_probe_t;

#define TINOS_WALLPAPER_DEFAULT  0u
#define TINOS_WALLPAPER_SOLID    1u
#define TINOS_WALLPAPER_GRADIENT 2u

#endif
