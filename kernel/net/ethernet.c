/* Ethernet framing over the e1000 device. */

#include <tinos/e1000.h>
#include <tinos/string.h>

#include "net_internal.h"

bool net_send_frame(const uint8_t *destination, uint16_t type,
                    const void *payload, size_t length)
{
    static uint8_t frame[NET_MTU + 64];
    if (!destination || !payload || length + ETH_HDR > sizeof(frame))
        return false;
    eth_hdr_t *eth = (eth_hdr_t *)frame;
    memcpy(eth->dst, destination, 6);
    memcpy(eth->src, net.mac, 6);
    eth->type = net_be16(type);
    memcpy(frame + ETH_HDR, payload, length);
    return e1000_send(frame, ETH_HDR + length);
}
