/* Networking core: device state, frame dispatch and public utilities. */

#include <tinos/e1000.h>
#include <tinos/bus.h>
#include <tinos/kernel.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/string.h>

#include "net_internal.h"

net_state_t net;

uint16_t net_be16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

uint32_t net_be32(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

uint16_t net_checksum(const void *data, size_t length)
{
    const uint8_t *bytes = data;
    uint32_t sum = 0;
    while (length >= 2) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)bytes[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

void net_poll(void)
{
    uint8_t frame[NET_MTU + 64];
    size_t length;
    while ((length = e1000_receive(frame, sizeof(frame))) != 0) {
        if (length < ETH_HDR)
            continue;
        const eth_hdr_t *eth = (const eth_hdr_t *)frame;
        uint16_t type = net_be16(eth->type);
        const uint8_t *payload = frame + ETH_HDR;
        size_t payload_length = length - ETH_HDR;
        if (type == 0x0806) {
            net_receive_arp(payload, payload_length);
            continue;
        }
        if (type != 0x0800 || payload_length < IP_HDR)
            continue;
        const ip_hdr_t *ip = (const ip_hdr_t *)payload;
        size_t header = (size_t)(ip->version_ihl & 0x0F) * 4;
        if (header < IP_HDR || header > payload_length ||
            (net.config.configured && ip->dst != net_be32(net.config.address)) ||
            (!net.config.configured && ip->dst != net_be32(0xFFFFFFFFu)))
            continue;
        net_receive_ipv4(ip, payload + header, payload_length - header);
    }
}

bool net_init(void)
{
    memset(&net, 0, sizeof(net));
    if (!e1000_available())
        return false;
    memcpy(net.mac, e1000_mac(), 6);
    net.initialized = true;
    return net_dhcp_exchange();
}

bool net_available(void) { return net.initialized && e1000_available(); }
const net_config_t *net_config(void) { return &net.config; }

bool net_request_dhcp(void)
{
    if (!net_available())
        return false;
    bool result = net_dhcp_exchange();
    if (result)
        bus_publish(&(bus_event_t){ BUS_NETWORK_CHANGED, 1, 0, 0 });
    return result;
}

bool net_configure_manual(uint32_t address, uint32_t netmask,
                          uint32_t gateway, uint32_t dns)
{
    if (!net_available() || !address || !netmask)
        return false;
    net.config.address = address;
    net.config.netmask = netmask;
    net.config.gateway = gateway;
    net.config.dns = dns;
    net.config.configured = true;
    net.arp_valid = false;
    bus_publish(&(bus_event_t){ BUS_NETWORK_CHANGED, 2, 0, 0 });
    return true;
}

void net_format_address(uint32_t address, char *out, size_t size)
{
    ksnprintf(out, size, "%u.%u.%u.%u", (address >> 24) & 255,
              (address >> 16) & 255, (address >> 8) & 255, address & 255);
}

bool net_parse_address(const char *text, uint32_t *out)
{
    uint32_t value = 0, part = 0;
    int dots = 0;
    const char *p = text;
    if (!text || !out)
        return false;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            part = part * 10 + (*p - '0');
            if (part > 255)
                return false;
        } else if (*p == '.') {
            if (dots >= 3)
                return false;
            value = (value << 8) | part;
            part = 0;
            dots++;
        } else {
            return false;
        }
        p++;
    }
    if (dots != 3)
        return false;
    *out = (value << 8) | part;
    return true;
}
