/* Internet Control Message Protocol echo support. */

#include <tinos/arch.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#include "net_internal.h"

void net_receive_icmp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (length < sizeof(icmp_hdr_t))
        return;
    const icmp_hdr_t *icmp = (const icmp_hdr_t *)data;
    if (net.ping_waiting && icmp->type == 0 && icmp->code == 0 &&
        (net_be32(icmp->rest) >> 16) == net.ping_id &&
        (net_be32(icmp->rest) & 0xFFFF) == net.ping_seq)
        net.ping_received = true;
    UNUSED(ip);
}

bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    if (!net.config.configured)
        return false;
    uint8_t data[sizeof(icmp_hdr_t) + 32];
    icmp_hdr_t *icmp = (icmp_hdr_t *)data;
    memset(data, 0, sizeof(data));
    icmp->type = 8;
    net.ping_id = 0x5449;
    net.ping_seq++;
    icmp->rest = net_be32(((uint32_t)net.ping_id << 16) | net.ping_seq);
    for (size_t i = sizeof(icmp_hdr_t); i < sizeof(data); i++)
        data[i] = (uint8_t)i;
    icmp->checksum = net_be16(net_checksum(data, sizeof(data)));
    net.ping_waiting = true;
    net.ping_received = false;
    uint32_t start = timer_ticks();
    bool sent = net_send_ipv4(address, 1, data, sizeof(data));
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (sent && timer_ticks() < deadline && !net.ping_received) {
        net_poll();
        arch_idle();
    }
    net.ping_waiting = false;
    if (elapsed_ms)
        *elapsed_ms = (uint32_t)timer_ticks() - start;
    return sent && net.ping_received;
}
