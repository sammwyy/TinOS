/* Small IPv4 network stack built on the e1000 driver. */
#ifndef TINOS_NET_H
#define TINOS_NET_H

#include <tinos/types.h>

typedef struct {
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    bool configured;
} net_config_t;

bool net_init(void);
void net_poll(void);
bool net_available(void);
const net_config_t *net_config(void);
void net_format_address(uint32_t address, char *out, size_t size);
bool net_parse_address(const char *text, uint32_t *out);
bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms);
bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms);
bool net_http_get(const char *host, const char *path, char *response,
                  size_t capacity, uint32_t timeout_ms);

#endif /* TINOS_NET_H */
