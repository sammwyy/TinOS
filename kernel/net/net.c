/* Ethernet, ARP, IPv4, UDP, DHCP, DNS and ICMP for the e1000 device. */

#include <tinos/arch.h>
#include <tinos/e1000.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/net.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#define NET_MTU 1500
#define ETH_HDR 14
#define IP_HDR 20
#define UDP_HDR 8
#define DHCP_CLIENT 68
#define DHCP_SERVER 67
#define DNS_PORT 53
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_ACK 0x10
#define TCP_HDR 20

typedef struct PACKED { uint8_t dst[6], src[6]; uint16_t type; } eth_hdr_t;
typedef struct PACKED {
    uint8_t version_ihl, tos; uint16_t length, id, flags_offset;
    uint8_t ttl, protocol; uint16_t checksum; uint32_t src, dst;
} ip_hdr_t;
typedef struct PACKED { uint16_t src, dst, length, checksum; } udp_hdr_t;
typedef struct PACKED {
    uint16_t src, dst; uint32_t seq, ack;
    uint8_t offset, flags; uint16_t window, checksum, urgent;
} tcp_hdr_t;
typedef struct PACKED { uint8_t type, code; uint16_t checksum; uint32_t rest; } icmp_hdr_t;
typedef struct PACKED {
    uint16_t htype, ptype; uint8_t hlen, plen; uint16_t operation;
    uint8_t sender_mac[6]; uint32_t sender_ip;
    uint8_t target_mac[6]; uint32_t target_ip;
} arp_pkt_t;

typedef struct PACKED {
    uint8_t op, htype, hlen, hops; uint32_t xid;
    uint16_t secs, flags; uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16]; uint8_t sname[64]; uint8_t file[128];
    uint8_t options[312];
} dhcp_pkt_t;

typedef struct {
    net_config_t config;
    uint8_t mac[6];
    uint8_t arp_mac[6];
    uint32_t arp_ip;
    bool arp_valid;
    uint32_t xid;
    uint16_t ip_id;
    bool initialized;
    bool ping_waiting, ping_received;
    uint16_t ping_id, ping_seq;
    bool dns_waiting, dns_received;
    uint16_t dns_id;
    uint32_t dns_answer;
    bool tcp_waiting, tcp_connected, tcp_closed;
    uint32_t tcp_ip, tcp_seq, tcp_ack;
    uint16_t tcp_local, tcp_remote;
    uint8_t *tcp_buffer;
    size_t tcp_length, tcp_capacity;
    bool dhcp_offer_received;
    uint32_t dhcp_offer, dhcp_mask, dhcp_gateway, dhcp_dns, dhcp_server;
} net_state_t;

static net_state_t net;
static uint8_t packet[NET_MTU + 64];

static uint16_t be16(uint16_t value)
{
    return (uint16_t)((value >> 8) | (value << 8));
}

static uint32_t be32(uint32_t value)
{
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static uint16_t checksum(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    while (length >= 2) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length)
        sum += (uint16_t)bytes[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool send_frame(const uint8_t *destination, uint16_t type,
                       const void *payload, size_t length)
{
    if (length + ETH_HDR > sizeof(packet))
        return false;
    eth_hdr_t *eth = (eth_hdr_t *)packet;
    memcpy(eth->dst, destination, 6);
    memcpy(eth->src, net.mac, 6);
    eth->type = be16(type);
    memcpy(packet + ETH_HDR, payload, length);
    return e1000_send(packet, ETH_HDR + length);
}

static void learn_arp(uint32_t address, const uint8_t *mac)
{
    net.arp_ip = address;
    memcpy(net.arp_mac, mac, 6);
    net.arp_valid = true;
}

static void send_arp_request(uint32_t address)
{
    static const uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    arp_pkt_t arp;
    memset(&arp, 0, sizeof(arp));
    arp.htype = be16(1); arp.ptype = be16(0x0800);
    arp.hlen = 6; arp.plen = 4; arp.operation = be16(1);
    memcpy(arp.sender_mac, net.mac, 6);
    arp.sender_ip = be32(net.config.address);
    arp.target_ip = be32(address);
    send_frame(broadcast, 0x0806, &arp, sizeof(arp));
}

static bool wait_for_arp(uint32_t address, uint32_t timeout)
{
    uint64_t deadline = timer_ticks() + timeout;
    send_arp_request(address);
    while (timer_ticks() < deadline) {
        net_poll();
        if (net.arp_valid && net.arp_ip == address)
            return true;
        __asm__ volatile("sti; hlt");
    }
    return false;
}

static bool resolve_mac(uint32_t destination, uint8_t *mac)
{
    if (net.arp_valid && net.arp_ip == destination) {
        memcpy(mac, net.arp_mac, 6);
        return true;
    }
    if (!wait_for_arp(destination, 1000))
        return false;
    memcpy(mac, net.arp_mac, 6);
    return true;
}

static uint32_t route(uint32_t address)
{
    if ((address & net.config.netmask) ==
        (net.config.address & net.config.netmask))
        return address;
    return net.config.gateway;
}

static bool send_ipv4(uint32_t destination, uint8_t protocol,
                      const void *payload, size_t payload_length)
{
    uint8_t mac[6];
    if (!resolve_mac(route(destination), mac))
        return false;
    if (IP_HDR + payload_length > NET_MTU)
        return false;
    uint8_t frame[NET_MTU];
    ip_hdr_t *ip = (ip_hdr_t *)frame;
    memset(ip, 0, IP_HDR);
    ip->version_ihl = 0x45;
    ip->length = be16((uint16_t)(IP_HDR + payload_length));
    ip->id = be16(net.ip_id++);
    ip->flags_offset = be16(0x4000);
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src = be32(net.config.address);
    ip->dst = be32(destination);
    ip->checksum = be16(checksum(ip, IP_HDR));
    memcpy(frame + IP_HDR, payload, payload_length);
    return send_frame(mac, 0x0800, frame, IP_HDR + payload_length);
}

static uint16_t udp_checksum(uint32_t source, uint32_t destination,
                             const udp_hdr_t *udp, const void *payload,
                             size_t length)
{
    uint8_t pseudo[12 + UDP_HDR + NET_MTU];
    if (length > NET_MTU - UDP_HDR)
        return 0;
    memcpy(pseudo, &source, 4);
    memcpy(pseudo + 4, &destination, 4);
    pseudo[8] = 0; pseudo[9] = 17;
    uint16_t total = be16((uint16_t)(UDP_HDR + length));
    memcpy(pseudo + 10, &total, 2);
    memcpy(pseudo + 12, udp, UDP_HDR);
    memcpy(pseudo + 12 + UDP_HDR, payload, length);
    return be16(checksum(pseudo, 12 + UDP_HDR + length));
}

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const tcp_hdr_t *tcp, const void *payload,
                             size_t length)
{
    uint8_t pseudo[12 + TCP_HDR + NET_MTU];
    if (length > NET_MTU - TCP_HDR)
        return 0;
    memcpy(pseudo, &source, 4);
    memcpy(pseudo + 4, &destination, 4);
    pseudo[8] = 0; pseudo[9] = 6;
    uint16_t total = be16((uint16_t)(TCP_HDR + length));
    memcpy(pseudo + 10, &total, 2);
    memcpy(pseudo + 12, tcp, TCP_HDR);
    memcpy(pseudo + 12 + TCP_HDR, payload, length);
    return be16(checksum(pseudo, 12 + TCP_HDR + length));
}

static bool send_udp(uint32_t destination, uint16_t source_port,
                     uint16_t destination_port, const void *payload,
                     size_t length)
{
    uint8_t data[UDP_HDR + NET_MTU];
    udp_hdr_t *udp = (udp_hdr_t *)data;
    udp->src = be16(source_port);
    udp->dst = be16(destination_port);
    udp->length = be16((uint16_t)(UDP_HDR + length));
    udp->checksum = 0;
    memcpy(data + UDP_HDR, payload, length);
    udp->checksum = udp_checksum(be32(net.config.address), be32(destination),
                                 udp, payload, length);
    return send_ipv4(destination, 17, data, UDP_HDR + length);
}

static bool send_tcp(uint32_t destination, uint16_t source_port,
                     uint16_t destination_port, uint32_t sequence,
                     uint32_t acknowledgment, uint8_t flags,
                     const void *payload, size_t length)
{
    uint8_t data[TCP_HDR + NET_MTU];
    tcp_hdr_t *tcp = (tcp_hdr_t *)data;
    if (length > NET_MTU - TCP_HDR) return false;
    memset(data, 0, TCP_HDR + length);
    tcp->src = be16(source_port); tcp->dst = be16(destination_port);
    tcp->seq = be32(sequence); tcp->ack = be32(acknowledgment);
    tcp->offset = 0x50; tcp->flags = flags; tcp->window = be16(4096);
    if (length) memcpy(data + TCP_HDR, payload, length);
    tcp->checksum = tcp_checksum(be32(net.config.address), be32(destination),
                                 tcp, payload, length);
    return send_ipv4(destination, 6, data, TCP_HDR + length);
}

static void dhcp_option(uint8_t **cursor, uint8_t code, const void *value,
                        uint8_t length)
{
    *(*cursor)++ = code;
    *(*cursor)++ = length;
    memcpy(*cursor, value, length);
    *cursor += length;
}

static bool dhcp_send(uint8_t message, uint32_t requested, uint32_t server)
{
    static const uint8_t broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    dhcp_pkt_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = 1; pkt.htype = 1; pkt.hlen = 6; pkt.xid = be32(net.xid);
    pkt.flags = be16(0x8000);
    memcpy(pkt.chaddr, net.mac, 6);
    uint8_t *o = pkt.options;
    uint32_t cookie = be32(0x63825363);
    memcpy(o, &cookie, 4); o += 4;
    dhcp_option(&o, 53, &message, 1);
    uint8_t client_id[7] = { 1, 0, 0, 0, 0, 0, 0 };
    memcpy(client_id + 1, net.mac, 6);
    dhcp_option(&o, 61, client_id, 7);
    if (requested) {
        uint32_t requested_net = be32(requested);
        dhcp_option(&o, 50, &requested_net, 4);
    }
    if (server) {
        uint32_t server_net = be32(server);
        dhcp_option(&o, 54, &server_net, 4);
    }
    uint8_t params[] = { 1, 3, 6, 15, 51, 54 };
    dhcp_option(&o, 55, params, sizeof(params));
    *o++ = 255;
    size_t dhcp_length = (sizeof(dhcp_pkt_t) - sizeof(pkt.options)) +
                         (size_t)(o - pkt.options);
    uint8_t frame[IP_HDR + UDP_HDR + sizeof(dhcp_pkt_t)];
    ip_hdr_t *ip = (ip_hdr_t *)frame;
    udp_hdr_t *udp = (udp_hdr_t *)(frame + IP_HDR);
    memset(ip, 0, IP_HDR);
    ip->version_ihl = 0x45;
    ip->length = be16((uint16_t)(IP_HDR + UDP_HDR + dhcp_length));
    ip->id = be16(net.ip_id++); ip->flags_offset = be16(0x4000);
    ip->ttl = 64; ip->protocol = 17; ip->dst = be32(0xFFFFFFFFu);
    ip->checksum = be16(checksum(ip, IP_HDR));
    udp->src = be16(DHCP_CLIENT); udp->dst = be16(DHCP_SERVER);
    udp->length = be16((uint16_t)(UDP_HDR + dhcp_length));
    /* DHCP starts before an IPv4 address exists; an all-zero UDP checksum is
     * explicitly valid and avoids using an invalid pseudo-header source. */
    udp->checksum = 0;
    memcpy(frame + IP_HDR + UDP_HDR, &pkt, dhcp_length);
    bool sent = send_frame(broadcast, 0x0800, frame,
                           IP_HDR + UDP_HDR + dhcp_length);
    return sent;
}

static bool dhcp_parse(const uint8_t *payload, size_t length, uint8_t wanted,
                       uint32_t *address, uint32_t *mask, uint32_t *gateway,
                       uint32_t *dns, uint32_t *server)
{
    if (length < sizeof(dhcp_pkt_t) - 312 || payload[0] != 2)
        return false;
    const dhcp_pkt_t *pkt = (const dhcp_pkt_t *)payload;
    if (pkt->xid != be32(net.xid) || pkt->hlen != 6 ||
        memcmp(pkt->chaddr, net.mac, 6) != 0)
        return false;
    uint8_t type = 0;
    const uint8_t *o = pkt->options + 4;
    const uint8_t *end = payload + length;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        if (o + 1 >= end || o + 2 + o[1] > end) break;
        if (*o == 53 && o[1] == 1) type = o[2];
        else if (*o == 1 && o[1] == 4) memcpy(mask, o + 2, 4);
        else if (*o == 3 && o[1] >= 4) memcpy(gateway, o + 2, 4);
        else if (*o == 6 && o[1] >= 4) memcpy(dns, o + 2, 4);
        else if (*o == 54 && o[1] == 4) memcpy(server, o + 2, 4);
        o += 2 + o[1];
    }
    if (type != wanted)
        return false;
    *address = pkt->yiaddr;
    return true;
}

static bool dhcp_exchange(void)
{
    net.xid = 0x54494E4Fu ^ (uint32_t)timer_ticks();
    net.config.address = net.config.netmask = net.config.gateway = 0;
    net.dhcp_offer_received = false;
    dhcp_send(1, 0, 0);
    uint64_t deadline = timer_ticks() + 3000;
    while (timer_ticks() < deadline && !net.dhcp_offer_received) {
        net_poll();
        __asm__ volatile("sti; hlt");
    }
    if (!net.dhcp_offer_received) {
        klog("[net ] DHCP offer timeout\n");
        return false;
    }
    dhcp_send(3, net.dhcp_offer, net.dhcp_server);
    deadline = timer_ticks() + 3000;
    while (timer_ticks() < deadline) {
        net_poll();
        __asm__ volatile("sti; hlt");
        if (net.config.configured)
            return true;
    }
    return false;
}

static void parse_arp(const uint8_t *data, size_t length)
{
    if (length < sizeof(arp_pkt_t)) return;
    const arp_pkt_t *arp = (const arp_pkt_t *)data;
    if (be16(arp->htype) == 1 && be16(arp->ptype) == 0x0800 &&
        arp->hlen == 6 && arp->plen == 4 && be16(arp->operation) == 2)
        learn_arp(be32(arp->sender_ip), arp->sender_mac);
}

static void parse_icmp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (length < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t *icmp = (const icmp_hdr_t *)data;
    if (net.ping_waiting && icmp->type == 0 && icmp->code == 0 &&
        (be32(icmp->rest) >> 16) == net.ping_id &&
        (be32(icmp->rest) & 0xFFFF) == net.ping_seq)
        net.ping_received = true;
    UNUSED(ip);
}

static void parse_udp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (length < UDP_HDR) return;
    const udp_hdr_t *udp = (const udp_hdr_t *)data;
    uint16_t source = be16(udp->src), destination = be16(udp->dst);
    const uint8_t *payload = data + UDP_HDR;
    size_t payload_length = length - UDP_HDR;
    if (source == DHCP_SERVER && destination == DHCP_CLIENT) {
        uint32_t address = 0, mask = 0, gateway = 0, dns = 0, server = 0;
        if (dhcp_parse(payload, payload_length, 2, &address, &mask, &gateway,
                       &dns, &server)) {
            net.dhcp_offer_received = true;
            net.dhcp_offer = be32(address);
            net.dhcp_mask = be32(mask);
            net.dhcp_gateway = be32(gateway);
            net.dhcp_dns = be32(dns);
            net.dhcp_server = be32(server);
        } else if (dhcp_parse(payload, payload_length, 5, &address, &mask,
                               &gateway, &dns, &server)) {
            net.config.address = be32(address);
            net.config.netmask = be32(mask);
            net.config.gateway = be32(gateway);
            net.config.dns = be32(dns);
            net.config.configured = true;
        }
    } else if (destination == 49152 && net.dns_waiting &&
               payload_length >= 12) {
        uint16_t id = (uint16_t)((payload[0] << 8) | payload[1]);
        uint16_t answers = (uint16_t)((payload[6] << 8) | payload[7]);
        if (id == net.dns_id && answers) {
            const uint8_t *p = payload + 12;
            while (p < payload + payload_length && *p) p += *p + 1;
            if (p + 5 < payload + payload_length) p += 5;
            for (uint16_t i = 0; i < answers && p + 12 <= payload + payload_length; i++) {
                if ((p[0] & 0xC0) == 0xC0) p += 2;
                else { while (p < payload + payload_length && *p) p += *p + 1; p++; }
                if (p + 10 > payload + payload_length) break;
                uint16_t type = (uint16_t)((p[0] << 8) | p[1]);
                uint16_t rdlen = (uint16_t)((p[8] << 8) | p[9]);
                if (type == 1 && rdlen == 4 && p + 10 + 4 <= payload + payload_length) {
                    net.dns_answer = ((uint32_t)p[10] << 24) | ((uint32_t)p[11] << 16) |
                                     ((uint32_t)p[12] << 8) | p[13];
                    net.dns_received = true;
                    return;
                }
                p += 10 + rdlen;
            }
        }
    }
    UNUSED(ip);
}

static void parse_tcp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (!net.tcp_waiting || length < TCP_HDR) return;
    const tcp_hdr_t *tcp = (const tcp_hdr_t *)data;
    if (be16(tcp->src) != net.tcp_remote || be16(tcp->dst) != net.tcp_local)
        return;
    uint32_t sequence = be32(tcp->seq);
    uint8_t flags = tcp->flags;
    size_t header = (size_t)(tcp->offset >> 4) * 4;
    if (header < TCP_HDR || header > length) return;
    size_t payload_length = length - header;
    if (!net.tcp_connected && (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                              (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        net.tcp_ack = sequence + 1;
        net.tcp_seq++;
        send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                 net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
        net.tcp_connected = true;
        return;
    }
    if (!net.tcp_connected || sequence != net.tcp_ack) return;
    if (payload_length && net.tcp_buffer) {
        size_t room = net.tcp_capacity - net.tcp_length;
        size_t copied = MIN(room, payload_length);
        memcpy(net.tcp_buffer + net.tcp_length, data + header, copied);
        net.tcp_length += copied;
        net.tcp_ack += (uint32_t)payload_length;
        send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                 net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
    }
    if (flags & TCP_FLAG_FIN) {
        net.tcp_ack++;
        send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                 net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
        net.tcp_closed = true;
    }
    UNUSED(ip);
}

void net_poll(void)
{
    uint8_t frame[NET_MTU + 64];
    size_t length;
    while ((length = e1000_receive(frame, sizeof(frame))) != 0) {
        if (length < ETH_HDR) continue;
        eth_hdr_t *eth = (eth_hdr_t *)frame;
        uint16_t type = be16(eth->type);
        const uint8_t *payload = frame + ETH_HDR;
        size_t payload_length = length - ETH_HDR;
        if (type == 0x0806) { parse_arp(payload, payload_length); continue; }
        if (type != 0x0800 || payload_length < IP_HDR) continue;
        const ip_hdr_t *ip = (const ip_hdr_t *)payload;
        size_t header = (ip->version_ihl & 0x0F) * 4;
        if (header < IP_HDR || header > payload_length ||
            (net.config.configured && ip->dst != be32(net.config.address)) ||
            (!net.config.configured && ip->dst != be32(0xFFFFFFFFu)))
            continue;
        const uint8_t *body = payload + header;
        size_t body_length = payload_length - header;
        if (ip->protocol == 1) parse_icmp(ip, body, body_length);
        else if (ip->protocol == 17) parse_udp(ip, body, body_length);
        else if (ip->protocol == 6) parse_tcp(ip, body, body_length);
    }
}

bool net_init(void)
{
    memset(&net, 0, sizeof(net));
    if (!e1000_available()) return false;
    memcpy(net.mac, e1000_mac(), 6);
    net.initialized = true;
    /* DHCP is intentionally attempted by the boot worker, not an IRQ. */
    return dhcp_exchange();
}

bool net_available(void) { return net.initialized && e1000_available(); }
const net_config_t *net_config(void) { return &net.config; }

void net_format_address(uint32_t address, char *out, size_t size)
{
    ksnprintf(out, size, "%u.%u.%u.%u", (address >> 24) & 255,
              (address >> 16) & 255, (address >> 8) & 255, address & 255);
}

bool net_parse_address(const char *text, uint32_t *out)
{
    uint32_t value = 0, part = 0; int dots = 0; const char *p = text;
    if (!text || !out) return false;
    while (*p) {
        if (*p >= '0' && *p <= '9') { part = part * 10 + (*p - '0'); if (part > 255) return false; }
        else if (*p == '.') { if (dots >= 3) return false; value = (value << 8) | part; part = 0; dots++; }
        else return false;
        p++;
    }
    if (dots != 3) return false;
    *out = (value << 8) | part;
    return true;
}

bool net_ping(uint32_t address, uint32_t timeout_ms, uint32_t *elapsed_ms)
{
    if (!net.config.configured) return false;
    uint8_t data[sizeof(icmp_hdr_t) + 32];
    icmp_hdr_t *icmp = (icmp_hdr_t *)data;
    memset(data, 0, sizeof(data));
    icmp->type = 8; icmp->code = 0;
    net.ping_id = 0x5449; net.ping_seq++;
    icmp->rest = be32(((uint32_t)net.ping_id << 16) | net.ping_seq);
    for (int i = sizeof(icmp_hdr_t); i < (int)sizeof(data); i++) data[i] = (uint8_t)i;
    icmp->checksum = be16(checksum(data, sizeof(data)));
    net.ping_waiting = true; net.ping_received = false;
    uint32_t start = (uint32_t)timer_ticks();
    bool sent = send_ipv4(address, 1, data, sizeof(data));
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (sent && timer_ticks() < deadline && !net.ping_received) { net_poll(); __asm__ volatile("sti; hlt"); }
    net.ping_waiting = false;
    if (elapsed_ms) *elapsed_ms = (uint32_t)timer_ticks() - start;
    return sent && net.ping_received;
}

bool net_dns_lookup(const char *name, uint32_t *address, uint32_t timeout_ms)
{
    if (!name || !address || !net.config.configured || !net.config.dns) return false;
    uint8_t query[256]; memset(query, 0, sizeof(query));
    net.dns_id = (uint16_t)(timer_ticks() ^ 0x4E53); query[0] = net.dns_id >> 8; query[1] = net.dns_id;
    query[2] = 1; query[5] = 1;
    uint8_t *p = query + 12; const char *label = name;
    while (*label) { const char *dot = label; while (*dot && *dot != '.') dot++; size_t n = (size_t)(dot - label); if (n > 63 || p + n + 2 >= query + sizeof(query)) return false; *p++ = (uint8_t)n; memcpy(p, label, n); p += n; label = *dot ? dot + 1 : dot; }
    *p++ = 0; *p++ = 0; *p++ = 1; *p++ = 0; *p++ = 1;
    net.dns_waiting = true; net.dns_received = false;
    bool sent = send_udp(net.config.dns, 49152, DNS_PORT, query, (size_t)(p - query));
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (sent && timer_ticks() < deadline && !net.dns_received) { net_poll(); __asm__ volatile("sti; hlt"); }
    net.dns_waiting = false;
    if (!sent || !net.dns_received) return false;
    *address = net.dns_answer; return true;
}

bool net_http_get(const char *host, const char *path, char *response,
                  size_t capacity, uint32_t timeout_ms)
{
    if (!host || !path || !response || capacity < 2 ||
        !net.config.configured) return false;
    uint32_t address;
    if (!net_parse_address(host, &address) &&
        !net_dns_lookup(host, &address, timeout_ms)) return false;
    static uint16_t next_port = 49153;
    net.tcp_ip = address; net.tcp_local = next_port++; net.tcp_remote = 80;
    net.tcp_seq = timer_ticks() ^ 0x54435031u;
    net.tcp_ack = 0; net.tcp_connected = false; net.tcp_closed = false;
    net.tcp_waiting = true; net.tcp_buffer = (uint8_t *)response;
    net.tcp_length = 0; net.tcp_capacity = capacity - 1;
    if (!send_tcp(address, net.tcp_local, net.tcp_remote, net.tcp_seq, 0,
                  TCP_FLAG_SYN, NULL, 0)) {
        net.tcp_waiting = false; return false;
    }
    net.tcp_seq++;
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (timer_ticks() < deadline && !net.tcp_connected) {
        net_poll(); __asm__ volatile("sti; hlt");
    }
    if (!net.tcp_connected) { net.tcp_waiting = false; return false; }
    char request[256];
    int written = ksnprintf(request, sizeof(request),
                            "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                            path[0] ? path : "/", host);
    if (written < 0 || (size_t)written >= sizeof(request) ||
        !send_tcp(address, net.tcp_local, net.tcp_remote, net.tcp_seq,
                  net.tcp_ack, TCP_FLAG_ACK, request, (size_t)written)) {
        net.tcp_waiting = false; return false;
    }
    net.tcp_seq += (uint32_t)written;
    deadline = timer_ticks() + timeout_ms;
    while (timer_ticks() < deadline && !net.tcp_closed) {
        net_poll(); __asm__ volatile("sti; hlt");
    }
    response[net.tcp_length] = 0;
    net.tcp_waiting = false;
    return net.tcp_length != 0;
}
