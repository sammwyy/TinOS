/*
 * USB Mass Storage Bulk-Only Transport and SCSI READ10/WRITE10.
 *
 * USB host controllers are transport providers. This layer deliberately knows
 * nothing about UHCI, EHCI, XHCI, addresses, endpoints, or enumeration; it
 * converts the standardized bulk protocol into a block_device_t.
 */

#include <tinos/kernel.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/sync.h>
#include <tinos/usb_msc.h>

#define MSC_CBW_SIGNATURE 0x43425355u
#define MSC_CSW_SIGNATURE 0x53425355u
#define MSC_CBW_LENGTH    31
#define MSC_CSW_LENGTH    13
#define MSC_MAX_TRANSFER  64

#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY   0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A

typedef struct PACKED {
    uint32_t signature;
    uint32_t tag;
    uint32_t transfer_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  command_length;
    uint8_t  command[16];
} msc_cbw_t;

typedef struct PACKED {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t  status;
} msc_csw_t;

typedef struct {
    usb_msc_transport_t *transport;
    uint32_t tag;
    block_device_t block;
    kmutex_t lock;
} msc_disk_t;

static msc_disk_t disk;

static bool transfer(msc_disk_t *msc, uint8_t command, uint32_t lba,
                     void *data, uint32_t length, bool write)
{
    bool locked = thread_current() != NULL;
    if (locked)
        kmutex_lock(&msc->lock);
    bool result = false;
    msc_cbw_t cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.signature = MSC_CBW_SIGNATURE;
    cbw.tag = ++msc->tag;
    cbw.transfer_length = length;
    cbw.flags = write ? 0 : 0x80;
    cbw.lun = msc->transport->lun;
    cbw.command_length = 10;
    cbw.command[0] = command;
    cbw.command[2] = (uint8_t)(lba >> 24);
    cbw.command[3] = (uint8_t)(lba >> 16);
    cbw.command[4] = (uint8_t)(lba >> 8);
    cbw.command[5] = (uint8_t)lba;
    cbw.command[7] = (uint8_t)(length / msc->transport->block_size >> 8);
    cbw.command[8] = (uint8_t)(length / msc->transport->block_size);

    if (!msc->transport->bulk_out(msc->transport->context, &cbw,
                                  MSC_CBW_LENGTH))
        goto done;
    if (length && data) {
        usb_msc_bulk_fn transfer_fn = write ? msc->transport->bulk_out :
                                              msc->transport->bulk_in;
        if (!transfer_fn(msc->transport->context, data, length))
            goto done;
    }

    msc_csw_t csw;
    memset(&csw, 0, sizeof(csw));
    if (!msc->transport->bulk_in(msc->transport->context, &csw,
                                 MSC_CSW_LENGTH))
        goto done;
    result = csw.signature == MSC_CSW_SIGNATURE && csw.tag == cbw.tag &&
             csw.status == 0;
done:
    if (locked)
        kmutex_unlock(&msc->lock);
    return result;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static bool msc_read(block_device_t *device, uint64_t lba, void *buffer,
                     uint32_t sectors)
{
    msc_disk_t *msc = device ? device->private_data : NULL;
    if (!msc || !buffer || lba + sectors > device->sector_count ||
        lba + sectors < lba || sectors > MSC_MAX_TRANSFER)
        return false;
    return transfer(msc, SCSI_READ10, (uint32_t)lba, buffer,
                    sectors * device->sector_size, false);
}

static bool msc_write(block_device_t *device, uint64_t lba, const void *buffer,
                      uint32_t sectors)
{
    msc_disk_t *msc = device ? device->private_data : NULL;
    if (!msc || !buffer || lba + sectors > device->sector_count ||
        lba + sectors < lba || sectors > MSC_MAX_TRANSFER)
        return false;
    return transfer(msc, SCSI_WRITE10, (uint32_t)lba, (void *)buffer,
                    sectors * device->sector_size, true);
}

bool usb_msc_init(block_device_t *device, usb_msc_transport_t *transport,
                  const char *name)
{
    if (!device || !transport || !transport->bulk_in ||
        !transport->bulk_out || transport->block_size != BLOCK_SECTOR_SIZE ||
        transport->block_count == 0)
        return false;
    memset(&disk, 0, sizeof(disk));
    kmutex_init(&disk.lock);
    disk.transport = transport;
    disk.block.sector_count = transport->block_count;
    disk.block.sector_size = transport->block_size;
    disk.block.read_only = false;
    disk.block.private_data = &disk;
    disk.block.read = msc_read;
    disk.block.write = msc_write;
    ksnprintf(disk.block.name, sizeof(disk.block.name), "%s",
              name ? name : "usb0");
    *device = disk.block;
    return true;
}

bool usb_msc_probe(block_device_t *device, usb_msc_transport_t *transport,
                   const char *name)
{
    if (!transport || !device || !transport->bulk_in ||
        !transport->bulk_out || transport->block_size == 0)
        return false;

    msc_disk_t probe;
    memset(&probe, 0, sizeof(probe));
    kmutex_init(&probe.lock);
    probe.transport = transport;
    if (!transfer(&probe, SCSI_TEST_UNIT_READY, 0, NULL, 0, false))
        return false;

    uint8_t capacity[8];
    if (!transfer(&probe, SCSI_READ_CAPACITY, 0, capacity, sizeof(capacity),
                  false))
        return false;
    uint32_t last_lba = be32(capacity);
    uint32_t block_size = be32(capacity + 4);
    if (block_size != BLOCK_SECTOR_SIZE || last_lba == UINT32_MAX)
        return false;
    transport->block_size = block_size;
    transport->block_count = last_lba + 1;
    return usb_msc_init(device, transport, name);
}
