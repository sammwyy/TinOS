/* User Datagram Protocol packet construction and receive dispatch. */

#include <tinos/string.h>

#include "net_internal.h"

static uint16_t udp_checksum(uint32_t source, uint32_t destination,
                             const udp_hdr_t *udp, const void *payload,
                             size_t length)
{
    uint8_t pseudo[12 + UDP_HDR + NET_MTU];
    if (length > NET_MTU - UDP_HDR)
        return 0;
    memcpy(pseudo, &source, 4);
    memcpy(pseudo + 4, &destination, 4);
    pseudo[8] = 0;
    pseudo[9] = 17;
    uint16_t total = net_be16((uint16_t)(UDP_HDR + length));
    memcpy(pseudo + 10, &total, 2);
    memcpy(pseudo + 12, udp, UDP_HDR);
    memcpy(pseudo + 12 + UDP_HDR, payload, length);
    return net_be16(net_checksum(pseudo, 12 + UDP_HDR + length));
}

bool net_send_udp(uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, const void *payload,
                  size_t length)
{
    if (length > NET_MTU - UDP_HDR)
        return false;
    uint8_t data[UDP_HDR + NET_MTU];
    udp_hdr_t *udp = (udp_hdr_t *)data;
    udp->src = net_be16(source_port);
    udp->dst = net_be16(destination_port);
    udp->length = net_be16((uint16_t)(UDP_HDR + length));
    udp->checksum = 0;
    memcpy(data + UDP_HDR, payload, length);
    udp->checksum = udp_checksum(net_be32(net.config.address),
                                 net_be32(destination), udp, payload, length);
    return net_send_ipv4(destination, 17, data, UDP_HDR + length);
}

void net_receive_udp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (length < UDP_HDR)
        return;
    const udp_hdr_t *udp = (const udp_hdr_t *)data;
    uint16_t source = net_be16(udp->src);
    uint16_t destination = net_be16(udp->dst);
    const uint8_t *payload = data + UDP_HDR;
    size_t payload_length = length - UDP_HDR;
    if (source == DHCP_SERVER && destination == DHCP_CLIENT)
        net_dhcp_receive(payload, payload_length);
    else if (destination == 49152)
        net_dns_receive(payload, payload_length);
    UNUSED(ip);
}
