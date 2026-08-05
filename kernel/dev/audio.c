#include <tinos/audio.h>
#include <tinos/arch.h>
#include <tinos/dma.h>
#include <tinos/driver.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>
#include <tinos/string.h>
#include <tinos/timer.h>

#define AC97_CLASS       0x04
#define AC97_SUBCLASS    0x01
#define AC97_VENDOR_ID  0x8086
#define AC97_DEVICE_ID  0x2415

#define AC97_MIXER_RESET 0x00
#define AC97_MASTER_VOL  0x02
#define AC97_PCM_VOL     0x18
#define AC97_EXT_CTRL    0x2a
#define AC97_PCM_RATE    0x2c

#define AC97_PO_BDBAR    0x10
#define AC97_PO_LVI      0x15
#define AC97_PO_SR       0x16
#define AC97_PO_CR       0x1b
#define AC97_GLOB_CNT    0x2c

#define AC97_RATE        22050u
#define AC97_MAX_FRAMES  (AC97_RATE * 2u)

typedef struct {
    uint32_t address;
    uint16_t length;
    uint16_t flags;
} ac97_bdle_t;

static uint16_t     mixer_base;
static uint16_t     bus_master_base;
static bool         available;
static dma_buffer_t descriptors;
static dma_buffer_t samples;

static uint16_t ac97_mixer_read(uint8_t offset)
{
    return inw((uint16_t)(mixer_base + offset));
}

static void ac97_mixer_write(uint8_t offset, uint16_t value)
{
    outw((uint16_t)(mixer_base + offset), value);
}

static bool ac97_match(const kernel_device_t *device)
{
    if (!device || device->bus != DEVICE_BUS_PCI)
        return false;
    const pci_device_t *pci = device->bus_data;
    return pci && pci->class_code == AC97_CLASS &&
           pci->subclass == AC97_SUBCLASS && pci->vendor_id == AC97_VENDOR_ID &&
           pci->device_id == AC97_DEVICE_ID;
}

static bool ac97_probe(kernel_device_t *device)
{
    const pci_device_t *pci = device->bus_data;
    uint32_t mixer = pci_bar(pci, 0);
    uint32_t bus_master = pci_bar(pci, 1);
    if (!mixer || !bus_master || !pci_enable_io(pci) ||
        !pci_enable_bus_master(pci))
        return false;

    mixer_base = (uint16_t)mixer;
    bus_master_base = (uint16_t)bus_master;

    outl((uint16_t)(bus_master_base + AC97_GLOB_CNT), 2);
    timer_sleep(2);
    outl((uint16_t)(bus_master_base + AC97_GLOB_CNT), 0);
    ac97_mixer_write(AC97_MIXER_RESET, 0);

    uint16_t ext = ac97_mixer_read(AC97_EXT_CTRL);
    ac97_mixer_write(AC97_EXT_CTRL, (uint16_t)(ext | 1));
    ac97_mixer_write(AC97_PCM_RATE, AC97_RATE);
    ac97_mixer_write(AC97_MASTER_VOL, 0);
    ac97_mixer_write(AC97_PCM_VOL, 0);

    if (!dma_alloc(&descriptors, sizeof(ac97_bdle_t)) ||
        !dma_alloc(&samples, AC97_MAX_FRAMES * 4u)) {
        dma_free(&descriptors);
        dma_free(&samples);
        return false;
    }
    dma_zero(&descriptors);
    dma_zero(&samples);
    available = true;
    klog("[audio] Intel AC'97 PCM output at I/O 0x%04x/0x%04x\n",
         mixer_base, bus_master_base);
    return true;
}

static void ac97_remove(kernel_device_t *device)
{
    (void)device;
    available = false;
    dma_free(&descriptors);
    dma_free(&samples);
    mixer_base = bus_master_base = 0;
}

static kernel_driver_t ac97_driver = {
    .name = "ac97-audio",
    .bus = DEVICE_BUS_PCI,
    .match = ac97_match,
    .probe = ac97_probe,
    .remove = ac97_remove,
};

void audio_driver_register(void)
{
    driver_register(&ac97_driver);
}

bool audio_available(void)
{
    return available;
}

static void write_sample(uint32_t frame, uint32_t frequency)
{
    uint16_t *pcm = (uint16_t *)samples.virt;
    uint32_t period = AC97_RATE / frequency;
    int16_t value = ((frame % period) < period / 2) ? 10000 : -10000;
    pcm[frame * 2] = (uint16_t)value;
    pcm[frame * 2 + 1] = (uint16_t)value;
}

bool audio_play_doremi(void)
{
    static const uint16_t notes[] = { 262, 294, 330, 349, 392, 440, 494, 523 };
    const uint32_t note_frames = AC97_RATE / 4;
    const uint32_t total_frames = note_frames * ARRAY_SIZE(notes);
    if (!available || total_frames > AC97_MAX_FRAMES)
        return false;

    uint16_t *pcm = (uint16_t *)samples.virt;
    memset(pcm, 0, samples.size);
    for (size_t note = 0; note < ARRAY_SIZE(notes); note++)
        for (uint32_t frame = 0; frame < note_frames; frame++)
            write_sample((uint32_t)note * note_frames + frame, notes[note]);

    ac97_bdle_t *bdle = (ac97_bdle_t *)descriptors.virt;
    bdle->address = (uint32_t)samples.phys;
    bdle->length = (uint16_t)total_frames;
    bdle->flags = 0x8000;

    outl((uint16_t)(bus_master_base + AC97_PO_BDBAR),
         (uint32_t)descriptors.phys);
    outw((uint16_t)(bus_master_base + AC97_PO_SR), 0x1c);
    outb((uint16_t)(bus_master_base + AC97_PO_LVI), 0);
    outb((uint16_t)(bus_master_base + AC97_PO_CR), 0x01);
    timer_sleep(ARRAY_SIZE(notes) * 250 + 30);
    outb((uint16_t)(bus_master_base + AC97_PO_CR), 0);
    return true;
}
