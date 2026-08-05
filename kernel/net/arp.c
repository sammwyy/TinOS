/* Address Resolution Protocol and the IPv4 neighbor cache. */

#include <tinos/io.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#include "net_internal.h"

static void learn(uint32_t address, const uint8_t *mac)
{
    net.arp_ip = address;
    memcpy(net.arp_mac, mac, 6);
    net.arp_valid = true;
}

static void request(uint32_t address)
{
    static const uint8_t broadcast[6] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    arp_pkt_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = net_be16(1);
    arp.ptype = net_be16(0x0800);
    arp.hlen = 6;
    arp.plen = 4;
    arp.operation = net_be16(1);
    memcpy(arp.sender_mac, net.mac, 6);
    arp.sender_ip = net_be32(net.config.address);
    arp.target_ip = net_be32(address);
    net_send_frame(broadcast, 0x0806, &arp, sizeof(arp));
}

static bool wait_for(uint32_t address, uint32_t timeout)
{
    uint64_t deadline = timer_ticks() + timeout;
    request(address);
    while (timer_ticks() < deadline) {
        net_poll();
        if (net.arp_valid && net.arp_ip == address)
            return true;
        __asm__ volatile("sti; hlt");
    }
    return false;
}

bool net_resolve_mac(uint32_t destination, uint8_t *mac)
{
    if (net.arp_valid && net.arp_ip == destination) {
        memcpy(mac, net.arp_mac, 6);
        return true;
    }
    if (!wait_for(destination, 1000))
        return false;
    memcpy(mac, net.arp_mac, 6);
    return true;
}

void net_receive_arp(const uint8_t *data, size_t length)
{
    if (length < sizeof(arp_pkt_t))
        return;
    const arp_pkt_t *arp = (const arp_pkt_t *)data;
    if (net_be16(arp->htype) == 1 && net_be16(arp->ptype) == 0x0800 &&
        arp->hlen == 6 && arp->plen == 4 && net_be16(arp->operation) == 2)
        learn(net_be32(arp->sender_ip), arp->sender_mac);
}
