/* Small DNS A-record resolver. */

#include <tinos/arch.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#include "net_internal.h"

void net_dns_receive(const uint8_t *payload, size_t length)
{
    if (!net.dns_waiting || length < 12)
        return;
    uint16_t id = (uint16_t)((payload[0] << 8) | payload[1]);
    uint16_t answers = (uint16_t)((payload[6] << 8) | payload[7]);
    if (id != net.dns_id || !answers)
        return;
    const uint8_t *p = payload + 12;
    while (p < payload + length && *p)
        p += *p + 1;
    if (p + 5 >= payload + length)
        return;
    p += 5;
    for (uint16_t i = 0; i < answers && p + 12 <= payload + length; i++) {
        if ((p[0] & 0xC0) == 0xC0)
            p += 2;
        else {
            while (p < payload + length && *p)
                p += *p + 1;
            p++;
        }
        if (p + 10 > payload + length)
            break;
        uint16_t type = (uint16_t)((p[0] << 8) | p[1]);
        uint16_t rdlength = (uint16_t)((p[8] << 8) | p[9]);
        if (type == 1 && rdlength == 4 && p + 14 <= payload + length) {
            net.dns_answer = ((uint32_t)p[10] << 24) |
                             ((uint32_t)p[11] << 16) |
                             ((uint32_t)p[12] << 8) | p[13];
            net.dns_received = true;
            return;
        }
        p += 10 + rdlength;
    }
}

bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms)
{
    if (!name || !address || !net.config.configured || !net.config.dns)
        return false;
    uint8_t query[256];
    memset(query, 0, sizeof(query));
    net.dns_id = (uint16_t)(timer_ticks() ^ 0x4E53);
    query[0] = net.dns_id >> 8;
    query[1] = net.dns_id;
    query[2] = 1;
    query[5] = 1;
    uint8_t *p = query + 12;
    const char *label = name;
    while (*label) {
        const char *dot = label;
        while (*dot && *dot != '.')
            dot++;
        size_t length = (size_t)(dot - label);
        if (length > 63 || p + length + 2 >= query + sizeof(query))
            return false;
        *p++ = (uint8_t)length;
        memcpy(p, label, length);
        p += length;
        label = *dot ? dot + 1 : dot;
    }
    *p++ = 0;
    *p++ = 0;
    *p++ = 1;
    *p++ = 0;
    *p++ = 1;
    net.dns_waiting = true;
    net.dns_received = false;
    bool sent = net_send_udp(net.config.dns, 49152, DNS_PORT, query,
                             (size_t)(p - query));
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (sent && timer_ticks() < deadline && !net.dns_received) {
        net_poll();
        arch_idle();
    }
    net.dns_waiting = false;
    if (!sent || !net.dns_received)
        return false;
    *address = net.dns_answer;
    return true;
}
