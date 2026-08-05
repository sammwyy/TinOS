/*
 * QEMU-compatible UHCI host controller path for USB mass storage.
 *
 * This is intentionally a single-device, polling implementation: one frame
 * list, direct TD chains, and synchronous control/bulk transfers. It provides
 * the smallest useful USB transport while keeping BOT/SCSI in usb_msc.c.
 */

#include <tinos/dma.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>
#include <tinos/string.h>
#include <tinos/thread.h>
#include <tinos/uhci.h>
#include <tinos/usb_msc.h>

#define UHCI_CLASS       0x0C
#define UHCI_SUBCLASS    0x03
#define UHCI_PROG_IF     0x00
#define UHCI_PORT_COUNT  2
#define UHCI_TD_COUNT    64
#define UHCI_TIMEOUT     1000000u

#define UHCI_CMD         0x00
#define UHCI_STATUS      0x02
#define UHCI_INTR        0x04
#define UHCI_FRAME       0x06
#define UHCI_FRAME_BASE  0x08
#define UHCI_SOF         0x0C
#define UHCI_PORTSC      0x10

#define UHCI_CMD_RUN     0x0001
#define UHCI_CMD_RESET   0x0002
#define UHCI_CMD_CONFIG  0x0040
#define UHCI_CMD_MAXP    0x0080

#define UHCI_PORT_CONNECT 0x0001
#define UHCI_PORT_ENABLE  0x0004
#define UHCI_PORT_RESET   0x0200

#define UHCI_LINK_TERM    0x00000001u
#define UHCI_LINK_QH      0x00000002u
#define UHCI_TD_ACTIVE    0x00800000u
#define UHCI_TD_ERROR     0x007E0000u

#define USB_PID_OUT       0xE1
#define USB_PID_IN        0x69
#define USB_PID_SETUP     0x2D

typedef struct PACKED {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
} uhci_td_t;

typedef struct PACKED {
    uint8_t  request_type;
    uint8_t  request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} usb_setup_t;

typedef struct {
    uint16_t io;
    uint8_t  address;
    uint8_t  interface_number;
    uint8_t  bulk_in;
    uint8_t  bulk_out;
    uint16_t bulk_in_max;
    uint16_t bulk_out_max;
    uint8_t  bulk_in_toggle;
    uint8_t  bulk_out_toggle;
    bool     low_speed;
    dma_buffer_t schedule;
    dma_buffer_t transfer;
    uhci_td_t *tds;
    uint32_t *frames;
    usb_msc_transport_t msc_transport;
    block_device_t block;
    bool ready;
    bool schedule_linked;
} uhci_state_t;

static uhci_state_t uhci;

static uint32_t address_of(const void *pointer)
{
    return (uint32_t)(uintptr_t)pointer;
}

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 2000000; i++)
        __asm__ volatile("pause");
}

static bool uhci_submit(size_t count)
{
    if (!uhci.schedule_linked) {
        for (size_t i = 0; i < 1024; i++)
            uhci.frames[i] = address_of(uhci.tds);
        uhci.schedule_linked = true;
    }
    __asm__ volatile("" ::: "memory");
    outw(uhci.io + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CONFIG | UHCI_CMD_MAXP);
    for (uint32_t i = 0; i < UHCI_TIMEOUT; i++) {
        bool active = false;
        for (size_t n = 0; n < count; n++)
            active |= (uhci.tds[n].status & UHCI_TD_ACTIVE) != 0;
        if (!active)
            break;
        io_wait();
        if ((i & 0x0FFFu) == 0x0FFFu)
            thread_yield();
    }
    for (size_t n = 0; n < count; n++)
        if ((uhci.tds[n].status & UHCI_TD_ACTIVE) ||
            (uhci.tds[n].status & UHCI_TD_ERROR)) {
            klog("[uhci] TD %u status 0x%08x token 0x%08x\n", (uint32_t)n,
                 uhci.tds[n].status, uhci.tds[n].token);
            klog("[uhci] submit cmd=0x%04x status=0x%04x frame=0x%04x\n",
                 inw(uhci.io + UHCI_CMD), inw(uhci.io + UHCI_STATUS),
                 inw(uhci.io + UHCI_FRAME));
            for (size_t i = 0; i < 1024; i++)
                uhci.frames[i] = UHCI_LINK_TERM;
            uhci.schedule_linked = false;
            return false;
        }
    return true;
}

static uint32_t token(uint8_t pid, uint8_t address, uint8_t endpoint,
                      uint8_t toggle, uint16_t length)
{
    uint32_t max_length = length ? (uint32_t)(length - 1) : 0x7FF;
    return pid | ((uint32_t)address << 8) | ((uint32_t)endpoint << 15) |
           ((uint32_t)(toggle & 1) << 19) | (max_length << 21);
}

static void setup_td(uhci_td_t *td, uint8_t pid, uint8_t address,
                     uint8_t endpoint, uint8_t toggle, uint16_t length,
                     uint32_t buffer, uint32_t next)
{
    td->link = next;
    td->status = UHCI_TD_ACTIVE | (3u << 27) |
                 (uhci.low_speed ? (1u << 26) : 0);
    td->token = token(pid, address, endpoint, toggle, length);
    td->buffer = buffer;
}

static bool control_transfer(uint8_t address, usb_setup_t setup, void *data,
                             uint16_t length, uint16_t max_packet)
{
    if (sizeof(setup) + length > uhci.transfer.size)
        return false;
    memcpy(uhci.transfer.virt, &setup, sizeof(setup));
    if (data && !(setup.request_type & 0x80))
        memcpy((uint8_t *)uhci.transfer.virt + 64, data, length);

    size_t count = 0;
    setup_td(&uhci.tds[count++], USB_PID_SETUP, address, 0, 0,
             sizeof(setup), uhci.transfer.phys, UHCI_LINK_TERM);
    uint8_t toggle = 1;
    uint8_t pid = (setup.request_type & 0x80) ? USB_PID_IN : USB_PID_OUT;
    uint16_t remaining = length;
    uint32_t offset = 64;
    while (remaining) {
        uint16_t packet = remaining < max_packet ? remaining : max_packet;
        setup_td(&uhci.tds[count++], pid, address, 0, toggle, packet,
                 uhci.transfer.phys + offset, UHCI_LINK_TERM);
        remaining -= packet;
        offset += packet;
        toggle ^= 1;
    }
    uint8_t status_pid = (setup.request_type & 0x80) ? USB_PID_OUT : USB_PID_IN;
    setup_td(&uhci.tds[count++], status_pid, address, 0, 1, 0, 0,
             UHCI_LINK_TERM);
    for (size_t i = 0; i + 1 < count; i++)
        uhci.tds[i].link = address_of(&uhci.tds[i + 1]);
    if (!uhci_submit(count))
        return false;
    if (data && (setup.request_type & 0x80))
        memcpy(data, (uint8_t *)uhci.transfer.virt + 64, length);
    return true;
}

static bool bulk_transfer(void *context, void *buffer, size_t length,
                          bool input)
{
    uhci_state_t *state = context;
    uint16_t max_packet = input ? state->bulk_in_max : state->bulk_out_max;
    uint8_t endpoint = input ? state->bulk_in : state->bulk_out;
    uint8_t *bytes = buffer;
    if (!state->ready || !max_packet || length > state->transfer.size)
        return false;
    if (!input)
        memcpy((uint8_t *)state->transfer.virt, buffer, length);

    uint8_t toggle = input ? state->bulk_in_toggle : state->bulk_out_toggle;
    size_t offset = 0;
    while (offset < length) {
        uint16_t packet = (uint16_t)MIN(length - offset, (size_t)max_packet);
        setup_td(&state->tds[0], input ? USB_PID_IN : USB_PID_OUT,
                 state->address, endpoint, toggle, packet,
                 state->transfer.phys + offset, UHCI_LINK_TERM);
        /* Keep an inactive tail in the schedule. QEMU's UHCI model, like
         * PIIX hardware, needs a following TD before it retires an async
         * bulk packet from a direct TD schedule. */
        state->tds[1].link = UHCI_LINK_TERM;
        state->tds[1].status = 0;
        state->tds[1].token = 0;
        state->tds[1].buffer = 0;
        if (!uhci_submit(2))
            return false;
        offset += packet;
        toggle ^= 1;
    }
    if (input)
        memcpy(bytes, (uint8_t *)state->transfer.virt, length);
    if (input)
        state->bulk_in_toggle = toggle;
    else
        state->bulk_out_toggle = toggle;
    return true;
}

static bool bulk_in(void *context, void *buffer, size_t length)
{
    return bulk_transfer(context, buffer, length, true);
}

static bool bulk_out(void *context, void *buffer, size_t length)
{
    return bulk_transfer(context, buffer, length, false);
}

static bool msc_reset(void *context)
{
    uhci_state_t *state = context;
    usb_setup_t setup = { 0x21, 0xFF, 0, state->interface_number, 0 };
    state->bulk_in_toggle = 0;
    state->bulk_out_toggle = 0;
    return control_transfer(state->address, setup, NULL, 0, 64);
}

static bool enumerate_port(uint16_t port)
{
    uint16_t status = inw(port);
    klog("[uhci] port 0x%04x status 0x%04x\n", port, status);
    if (!(status & UHCI_PORT_CONNECT))
        return false;
    uhci.low_speed = (status & 0x0100) != 0;
    outw(port, status | UHCI_PORT_RESET);
    delay();
    status = inw(port);
    uhci.low_speed = (status & 0x0100) != 0;
    klog("[uhci] port after reset 0x%04x\n", status);
    outw(port, (uint16_t)((status | UHCI_PORT_ENABLE) & ~UHCI_PORT_RESET));
    delay();
    status = inw(port);
    klog("[uhci] port enabled 0x%04x\n", status);

    uint8_t descriptor[512];
    usb_setup_t setup = { 0x80, 6, 0x0100, 0, 8 };
    if (!control_transfer(0, setup, descriptor, 8, 8)) {
        klog("[uhci] device descriptor failed\n");
        return false;
    }
    uint16_t max_packet = descriptor[7];
    if (max_packet != 8 && max_packet != 16 && max_packet != 32 &&
        max_packet != 64)
        return false;

    setup = (usb_setup_t){ 0, 5, 1, 0, 0 };
    if (!control_transfer(0, setup, NULL, 0, max_packet)) {
        klog("[uhci] set address failed\n");
        return false;
    }
    uhci.address = 1;
    setup = (usb_setup_t){ 0x80, 6, 0x0200, 0, 9 };
    if (!control_transfer(uhci.address, setup, descriptor, 9, max_packet)) {
        klog("[uhci] config header failed\n");
        return false;
    }
    uint16_t total = (uint16_t)descriptor[2] | ((uint16_t)descriptor[3] << 8);
    if (total > sizeof(descriptor))
        return false;
    setup.length = total;
    if (!control_transfer(uhci.address, setup, descriptor, total, max_packet)) {
        klog("[uhci] config body failed\n");
        return false;
    }

    uint8_t config_value = 0;
    uint8_t interface = 0;
    uint8_t in_endpoint = 0, out_endpoint = 0;
    uint16_t in_max = 0, out_max = 0;
    for (uint16_t offset = 0; offset + 2 <= total;) {
        uint8_t length = descriptor[offset];
        uint8_t type = descriptor[offset + 1];
        if (length < 2 || offset + length > total)
            break;
        if (type == 2 && length >= 6)
            config_value = descriptor[offset + 5];
        if (type == 4 && length >= 9 && descriptor[offset + 5] == 8 &&
            descriptor[offset + 6] == 6 && descriptor[offset + 7] == 0x50)
            interface = descriptor[offset + 2];
        if (type == 5 && length >= 7 && descriptor[offset + 3] == 2) {
            uint8_t endpoint = descriptor[offset + 2];
            uint16_t packet = (uint16_t)descriptor[offset + 4] |
                              ((uint16_t)descriptor[offset + 5] << 8);
            if (endpoint & 0x80) {
                in_endpoint = endpoint & 0x0F;
                in_max = packet;
            } else {
                out_endpoint = endpoint & 0x0F;
                out_max = packet;
            }
        }
        offset += length;
    }
    if (!config_value || !in_endpoint || !out_endpoint || !in_max || !out_max) {
        klog("[uhci] no BOT interface config=%u in=%u out=%u\n", config_value,
             in_endpoint, out_endpoint);
        return false;
    }

    setup = (usb_setup_t){ 0, 9, config_value, 0, 0 };
    if (!control_transfer(uhci.address, setup, NULL, 0, max_packet)) {
        klog("[uhci] set configuration failed\n");
        return false;
    }
    uhci.interface_number = interface;
    uhci.bulk_in = in_endpoint;
    uhci.bulk_out = out_endpoint;
    uhci.bulk_in_max = in_max;
    uhci.bulk_out_max = out_max;
    uhci.bulk_in_toggle = 0;
    uhci.bulk_out_toggle = 0;
    uhci.ready = true;
    if (!msc_reset(&uhci)) {
        klog("[uhci] BOT reset failed\n");
        return false;
    }
    uhci.msc_transport = (usb_msc_transport_t){
        .context = &uhci,
        .bulk_in = bulk_in,
        .bulk_out = bulk_out,
        .reset = msc_reset,
        .lun = 0,
        .block_size = BLOCK_SECTOR_SIZE,
    };
    if (!usb_msc_probe(&uhci.block, &uhci.msc_transport, "usb0")) {
        klog("[uhci] MSC capacity probe failed\n");
        return false;
    }
    klog("[uhci] USB mass storage ready\n");
    return true;
}

size_t uhci_usb_init(void)
{
    memset(&uhci, 0, sizeof(uhci));
    pci_device_t pci;
    if (!pci_find_class(UHCI_CLASS, UHCI_SUBCLASS, 0, &pci) ||
        pci.prog_if != UHCI_PROG_IF || !pci_enable_io(&pci) ||
        !pci_enable_bus_master(&pci))
        return 0;
    uint32_t bar = pci_bar(&pci, 4);
    if (!bar)
        return 0;
    klog("[uhci] controller %02x:%02x.%u io 0x%04x\n", pci.bus, pci.slot,
         pci.func, (uint16_t)bar);
    uhci.io = (uint16_t)bar;
    outw(uhci.io + UHCI_CMD, UHCI_CMD_RESET);
    delay();
    outw(uhci.io + UHCI_CMD, 0);
    outw(uhci.io + UHCI_STATUS, 0xFFFF);
    outw(uhci.io + UHCI_INTR, 0);
    if (!dma_alloc(&uhci.schedule, 8192) || !dma_alloc(&uhci.transfer, 65536))
        return 0;
    dma_zero(&uhci.schedule);
    dma_zero(&uhci.transfer);
    uhci.frames = uhci.schedule.virt;
    uhci.tds = (void *)((uintptr_t)uhci.schedule.virt + 8192 -
                        UHCI_TD_COUNT * sizeof(uhci_td_t));
    for (size_t i = 0; i < 1024; i++)
        uhci.frames[i] = UHCI_LINK_TERM;
    outl(uhci.io + UHCI_FRAME_BASE, uhci.schedule.phys);
    outw(uhci.io + UHCI_FRAME, 0);
    outb(uhci.io + UHCI_SOF, 64);
    outw(uhci.io + UHCI_CMD, UHCI_CMD_RUN | UHCI_CMD_CONFIG | UHCI_CMD_MAXP);
    delay();
    klog("[uhci] schedule phys=0x%08x framebase=0x%08x cmd=0x%04x status=0x%04x\n",
         (uint32_t)uhci.schedule.phys, inl(uhci.io + UHCI_FRAME_BASE),
         inw(uhci.io + UHCI_CMD), inw(uhci.io + UHCI_STATUS));
    klog("[uhci] td=0x%08x frame0=0x%08x\n",
         address_of(uhci.tds), uhci.frames[0]);
    for (size_t port = 0; port < UHCI_PORT_COUNT; port++)
        if (enumerate_port((uint16_t)(uhci.io + UHCI_PORTSC + port * 2)))
            return 1;
    dma_free(&uhci.schedule);
    dma_free(&uhci.transfer);
    return 0;
}

size_t uhci_usb_count(void)
{
    return uhci.ready ? 1 : 0;
}

block_device_t *uhci_usb_device(size_t index)
{
    return index == 0 && uhci.ready ? &uhci.block : NULL;
}
