/* Minimal TCP client used by the HTTP helper. */

#include <tinos/string.h>
#include <tinos/timer.h>
#include <tinos/printf.h>

#include "net_internal.h"

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const tcp_hdr_t *tcp, const void *payload,
                             size_t length)
{
    uint8_t pseudo[12 + TCP_HDR + NET_MTU];
    if (length > NET_MTU - TCP_HDR)
        return 0;
    memcpy(pseudo, &source, 4);
    memcpy(pseudo + 4, &destination, 4);
    pseudo[8] = 0;
    pseudo[9] = 6;
    uint16_t total = net_be16((uint16_t)(TCP_HDR + length));
    memcpy(pseudo + 10, &total, 2);
    memcpy(pseudo + 12, tcp, TCP_HDR);
    memcpy(pseudo + 12 + TCP_HDR, payload, length);
    return net_be16(net_checksum(pseudo, 12 + TCP_HDR + length));
}

bool net_send_tcp(uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, uint32_t sequence,
                  uint32_t acknowledgment, uint8_t flags,
                  const void *payload, size_t length)
{
    if (length > NET_MTU - TCP_HDR)
        return false;
    uint8_t data[TCP_HDR + NET_MTU];
    tcp_hdr_t *tcp = (tcp_hdr_t *)data;
    memset(data, 0, TCP_HDR + length);
    tcp->src = net_be16(source_port);
    tcp->dst = net_be16(destination_port);
    tcp->seq = net_be32(sequence);
    tcp->ack = net_be32(acknowledgment);
    tcp->offset = 0x50;
    tcp->flags = flags;
    tcp->window = net_be16(4096);
    memcpy(data + TCP_HDR, payload, length);
    tcp->checksum = tcp_checksum(net_be32(net.config.address),
                                 net_be32(destination), tcp, payload, length);
    return net_send_ipv4(destination, 6, data, TCP_HDR + length);
}

void net_receive_tcp(const ip_hdr_t *ip, const uint8_t *data, size_t length)
{
    if (!net.tcp_waiting || length < TCP_HDR)
        return;
    const tcp_hdr_t *tcp = (const tcp_hdr_t *)data;
    if (net_be16(tcp->src) != net.tcp_remote ||
        net_be16(tcp->dst) != net.tcp_local)
        return;
    uint32_t sequence = net_be32(tcp->seq);
    uint8_t flags = tcp->flags;
    size_t header = (size_t)(tcp->offset >> 4) * 4;
    if (header < TCP_HDR || header > length)
        return;
    size_t payload_length = length - header;
    if (!net.tcp_connected &&
        (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
            (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        net.tcp_ack = sequence + 1;
        net.tcp_seq++;
        net_send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                     net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
        net.tcp_connected = true;
        return;
    }
    if (!net.tcp_connected || sequence != net.tcp_ack)
        return;
    if (payload_length && net.tcp_buffer) {
        size_t room = net.tcp_capacity - net.tcp_length;
        size_t copied = MIN(room, payload_length);
        memcpy(net.tcp_buffer + net.tcp_length, data + header, copied);
        net.tcp_length += copied;
        net.tcp_ack += (uint32_t)payload_length;
        net_send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                     net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
    }
    if (flags & TCP_FLAG_FIN) {
        net.tcp_ack++;
        net_send_tcp(net.tcp_ip, net.tcp_local, net.tcp_remote, net.tcp_seq,
                     net.tcp_ack, TCP_FLAG_ACK, NULL, 0);
        net.tcp_closed = true;
    }
    UNUSED(ip);
}

bool net_http_get(const char *host, const char *path, char *response,
                  size_t capacity, uint32_t timeout_ms)
{
    if (!host || !path || !response || capacity < 2 ||
        !net.config.configured)
        return false;
    uint32_t address;
    if (!net_parse_address(host, &address) &&
        !net_dns_lookup(host, &address, timeout_ms))
        return false;
    static uint16_t next_port = 49153;
    net.tcp_ip = address;
    net.tcp_local = next_port++;
    net.tcp_remote = 80;
    net.tcp_seq = timer_ticks() ^ 0x54435031u;
    net.tcp_ack = 0;
    net.tcp_connected = false;
    net.tcp_closed = false;
    net.tcp_waiting = true;
    net.tcp_buffer = (uint8_t *)response;
    net.tcp_length = 0;
    net.tcp_capacity = capacity - 1;
    if (!net_send_tcp(address, net.tcp_local, net.tcp_remote, net.tcp_seq, 0,
                      TCP_FLAG_SYN, NULL, 0)) {
        net.tcp_waiting = false;
        return false;
    }
    net.tcp_seq++;
    uint64_t deadline = timer_ticks() + timeout_ms;
    while (timer_ticks() < deadline && !net.tcp_connected) {
        net_poll();
        __asm__ volatile("sti; hlt");
    }
    if (!net.tcp_connected) {
        net.tcp_waiting = false;
        return false;
    }
    char request[256];
    int written = ksnprintf(request, sizeof(request),
                            "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                            path[0] ? path : "/", host);
    if (written < 0 || (size_t)written >= sizeof(request) ||
        !net_send_tcp(address, net.tcp_local, net.tcp_remote, net.tcp_seq,
                      net.tcp_ack, TCP_FLAG_ACK, request, (size_t)written)) {
        net.tcp_waiting = false;
        return false;
    }
    net.tcp_seq += (uint32_t)written;
    deadline = timer_ticks() + timeout_ms;
    while (timer_ticks() < deadline && !net.tcp_closed) {
        net_poll();
        __asm__ volatile("sti; hlt");
    }
    response[net.tcp_length] = 0;
    net.tcp_waiting = false;
    return net.tcp_length != 0;
}
