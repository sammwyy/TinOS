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

typedef enum {
    NET_JOB_PING,
    NET_JOB_DNS,
    NET_JOB_HTTP,
} net_job_kind_t;

typedef struct {
    uint32_t id;
    bool success;
    char text[2048];
} net_job_result_t;

bool net_init(void);
void net_poll(void);
bool net_available(void);
const net_config_t *net_config(void);
bool net_request_dhcp(void);
bool net_configure_manual(uint32_t address, uint32_t netmask,
                          uint32_t gateway, uint32_t dns);
void net_format_address(uint32_t address, char *out, size_t size);
bool net_parse_address(const char *text, uint32_t *out);
bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms);
bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms);
bool net_http_get(const char *host, const char *path, char *response,
                  size_t capacity, uint32_t timeout_ms);
bool net_async_init(void);
uint32_t net_async_submit(net_job_kind_t kind, const char *target,
                          const char *path);
bool net_async_take_result(net_job_result_t *result);

#endif /* TINOS_NET_H */
