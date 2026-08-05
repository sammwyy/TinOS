/* IPv4 routing, packet construction and protocol dispatch. */

#include <tinos/string.h>

#include "net_internal.h"

static uint32_t route(uint32_t address)
{
    if ((address & net.config.netmask) ==
        (net.config.address & net.config.netmask))
        return address;
    return net.config.gateway;
}

bool net_send_ipv4(uint32_t destination, uint8_t protocol,
                   const void *payload, size_t payload_length)
{
    uint8_t mac[6];
    if (IP_HDR + payload_length > NET_MTU ||
        !net_resolve_mac(route(destination), mac))
        return false;
    uint8_t frame[NET_MTU];
    ip_hdr_t *ip = (ip_hdr_t *)frame;
    memset(ip, 0, IP_HDR);
    ip->version_ihl = 0x45;
    ip->length = net_be16((uint16_t)(IP_HDR + payload_length));
    ip->id = net_be16(net.ip_id++);
    ip->flags_offset = net_be16(0x4000);
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src = net_be32(net.config.address);
    ip->dst = net_be32(destination);
    ip->checksum = net_be16(net_checksum(ip, IP_HDR));
    memcpy(frame + IP_HDR, payload, payload_length);
    return net_send_frame(mac, 0x0800, frame, IP_HDR + payload_length);
}

void net_receive_ipv4(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (ip->protocol == 1)
        net_receive_icmp(ip, data, length);
    else if (ip->protocol == 17)
        net_receive_udp(ip, data, length);
    else if (ip->protocol == 6)
        net_receive_tcp(ip, data, length);
}
