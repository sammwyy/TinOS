/* DHCP client and lease parsing. */

#include <tinos/arch.h>
#include <tinos/kernel.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#include "net_internal.h"

static void option(uint8_t **cursor, uint8_t code, const void *value,
                   uint8_t length)
{
    *(*cursor)++ = code;
    *(*cursor)++ = length;
    memcpy(*cursor, value, length);
    *cursor += length;
}

static bool send_dhcp(uint8_t message, uint32_t requested, uint32_t server)
{
    static const uint8_t broadcast[6] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    dhcp_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1;
    pkt.htype = 1;
    pkt.hlen = 6;
    pkt.xid = net_be32(net.xid);
    pkt.flags = net_be16(0x8000);
    memcpy(pkt.chaddr, net.mac, 6);
    uint8_t *cursor = pkt.options;
    uint32_t cookie = net_be32(0x63825363);
    memcpy(cursor, &cookie, 4);
    cursor += 4;
    option(&cursor, 53, &message, 1);
    uint8_t client_id[7] = { 1, 0, 0, 0, 0, 0, 0 };
    memcpy(client_id + 1, net.mac, 6);
    option(&cursor, 61, client_id, sizeof(client_id));
    if (requested) {
        uint32_t value = net_be32(requested);
        option(&cursor, 50, &value, 4);
    }
    if (server) {
        uint32_t value = net_be32(server);
        option(&cursor, 54, &value, 4);
    }
    uint8_t parameters[] = { 1, 3, 6, 15, 51, 54 };
    option(&cursor, 55, parameters, sizeof(parameters));
    *cursor++ = 255;
    size_t dhcp_length = sizeof(pkt) - sizeof(pkt.options) +
                         (size_t)(cursor - pkt.options);
    uint8_t frame[IP_HDR + UDP_HDR + sizeof(dhcp_pkt_t)];
    ip_hdr_t *ip = (ip_hdr_t *)frame;
    udp_hdr_t *udp = (udp_hdr_t *)(frame + IP_HDR);
    memset(ip, 0, IP_HDR);
    ip->version_ihl = 0x45;
    ip->length = net_be16((uint16_t)(IP_HDR + UDP_HDR + dhcp_length));
    ip->id = net_be16(net.ip_id++);
    ip->flags_offset = net_be16(0x4000);
    ip->ttl = 64;
    ip->protocol = 17;
    ip->dst = net_be32(0xFFFFFFFFu);
    ip->checksum = net_be16(net_checksum(ip, IP_HDR));
    udp->src = net_be16(DHCP_CLIENT);
    udp->dst = net_be16(DHCP_SERVER);
    udp->length = net_be16((uint16_t)(UDP_HDR + dhcp_length));
    udp->checksum = 0;
    memcpy(frame + IP_HDR + UDP_HDR, &pkt, dhcp_length);
    return net_send_frame(broadcast, 0x0800, frame,
                           IP_HDR + UDP_HDR + dhcp_length);
}

static bool parse_dhcp(const uint8_t *payload, size_t length, uint8_t wanted,
                       uint32_t *address, uint32_t *mask,
                       uint32_t *gateway, uint32_t *dns, uint32_t *server)
{
    if (length < sizeof(dhcp_pkt_t) - 312 || payload[0] != 2)
        return false;
    const dhcp_pkt_t *pkt = (const dhcp_pkt_t *)payload;
    if (pkt->xid != net_be32(net.xid) || pkt->hlen != 6 ||
        memcmp(pkt->chaddr, net.mac, 6) != 0)
        return false;
    uint8_t type = 0;
    const uint8_t *cursor = pkt->options + 4;
    const uint8_t *end = payload + length;
    while (cursor < end && *cursor != 255) {
        if (*cursor == 0) {
            cursor++;
            continue;
        }
        if (cursor + 1 >= end || cursor + 2 + cursor[1] > end)
            break;
        if (*cursor == 53 && cursor[1] == 1)
            type = cursor[2];
        else if (*cursor == 1 && cursor[1] == 4)
            memcpy(mask, cursor + 2, 4);
        else if (*cursor == 3 && cursor[1] >= 4)
            memcpy(gateway, cursor + 2, 4);
        else if (*cursor == 6 && cursor[1] >= 4)
            memcpy(dns, cursor + 2, 4);
        else if (*cursor == 54 && cursor[1] == 4)
            memcpy(server, cursor + 2, 4);
        cursor += 2 + cursor[1];
    }
    if (type != wanted)
        return false;
    *address = pkt->yiaddr;
    return true;
}

void net_dhcp_receive(const uint8_t *payload, size_t length)
{
    uint32_t address = 0, mask = 0, gateway = 0, dns = 0, server = 0;
    if (parse_dhcp(payload, length, 2, &address, &mask, &gateway, &dns,
                   &server)) {
        net.dhcp_offer_received = true;
        net.dhcp_offer = net_be32(address);
        net.dhcp_mask = net_be32(mask);
        net.dhcp_gateway = net_be32(gateway);
        net.dhcp_dns = net_be32(dns);
        net.dhcp_server = net_be32(server);
    } else if (parse_dhcp(payload, length, 5, &address, &mask, &gateway,
                          &dns, &server)) {
        net.config.address = net_be32(address);
        net.config.netmask = net_be32(mask);
        net.config.gateway = net_be32(gateway);
        net.config.dns = net_be32(dns);
        net.config.configured = true;
    }
}

bool net_dhcp_exchange(void)
{
    net.xid = 0x54494E4Fu ^ (uint32_t)timer_ticks();
    net.config.address = net.config.netmask = net.config.gateway = 0;
    net.config.dns = 0;
    net.config.configured = false;
    net.dhcp_offer_received = false;
    send_dhcp(1, 0, 0);
    uint64_t deadline = timer_ticks() + 3000;
    while (timer_ticks() < deadline && !net.dhcp_offer_received) {
        net_poll();
        arch_idle();
    }
    if (!net.dhcp_offer_received) {
        klog("[net ] DHCP offer timeout\n");
        return false;
    }
    send_dhcp(3, net.dhcp_offer, net.dhcp_server);
    deadline = timer_ticks() + 3000;
    while (timer_ticks() < deadline) {
        net_poll();
        arch_idle();
        if (net.config.configured)
            return true;
    }
    return false;
}
