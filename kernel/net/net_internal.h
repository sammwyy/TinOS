/* Private interfaces shared by the networking protocol modules. */
#ifndef TINOS_NET_INTERNAL_H
#define TINOS_NET_INTERNAL_H

#include <tinos/net.h>
#include <tinos/kernel.h>
#include <tinos/arch.h>

#define NET_MTU       1500
#define ETH_HDR       14
#define IP_HDR        20
#define UDP_HDR       8
#define DHCP_CLIENT   68
#define DHCP_SERVER   67
#define DNS_PORT      53
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_ACK  0x10
#define TCP_HDR       20

typedef struct PACKED {
    uint8_t dst[6], src[6];
    uint16_t type;
} eth_hdr_t;

typedef struct PACKED {
    uint8_t version_ihl, tos;
    uint16_t length, id, flags_offset;
    uint8_t ttl, protocol;
    uint16_t checksum;
    uint32_t src, dst;
} ip_hdr_t;

typedef struct PACKED {
    uint16_t src, dst, length, checksum;
} udp_hdr_t;

typedef struct PACKED {
    uint16_t src, dst;
    uint32_t seq, ack;
    uint8_t offset, flags;
    uint16_t window, checksum, urgent;
} tcp_hdr_t;

typedef struct PACKED {
    uint8_t type, code;
    uint16_t checksum;
    uint32_t rest;
} icmp_hdr_t;

typedef struct PACKED {
    uint16_t htype, ptype;
    uint8_t hlen, plen;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} arp_pkt_t;

typedef struct PACKED {
    uint8_t op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t chaddr[16], sname[64], file[128], options[312];
} dhcp_pkt_t;

typedef struct {
    net_config_t config;
    uint8_t mac[6], arp_mac[6];
    uint32_t arp_ip, xid;
    uint16_t ip_id;
    bool arp_valid, initialized;
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

extern net_state_t net;

uint16_t net_be16(uint16_t value);
uint32_t net_be32(uint32_t value);
uint16_t net_checksum(const void *data, size_t length);
bool net_send_frame(const uint8_t *destination, uint16_t type,
                    const void *payload, size_t length);
bool net_send_ipv4(uint32_t destination, uint8_t protocol,
                   const void *payload, size_t payload_length);
void net_receive_ipv4(const ip_hdr_t *ip, const uint8_t *data, size_t length);
void net_receive_arp(const uint8_t *data, size_t length);
void net_receive_udp(const ip_hdr_t *ip, const uint8_t *data, size_t length);
void net_receive_icmp(const ip_hdr_t *ip, const uint8_t *data, size_t length);
void net_receive_tcp(const ip_hdr_t *ip, const uint8_t *data, size_t length);

bool net_resolve_mac(uint32_t destination, uint8_t *mac);
bool net_send_udp(uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, const void *payload,
                  size_t length);
bool net_send_tcp(uint32_t destination, uint16_t source_port,
                  uint16_t destination_port, uint32_t sequence,
                  uint32_t acknowledgment, uint8_t flags,
                  const void *payload, size_t length);
bool net_dhcp_exchange(void);
void net_dhcp_receive(const uint8_t *payload, size_t length);
void net_dns_receive(const uint8_t *payload, size_t length);

#endif /* TINOS_NET_INTERNAL_H */
