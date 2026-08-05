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
#define AC97_PO_CIV      0x14
#define AC97_PO_LVI      0x15
#define AC97_PO_SR       0x16
#define AC97_PO_CR       0x1b
#define AC97_GLOB_CNT    0x2c

#define AC97_RATE        22050u
#define AC97_BUFFER_FRAMES 2048u
#define AC97_BUFFER_COUNT 4u

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
static uint32_t queued_frames;
static uint64_t played_frames;
static uint8_t active_buffer;
static bool stream_active;
static bool stream_paused;

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

    if (!dma_alloc(&descriptors, sizeof(ac97_bdle_t) * AC97_BUFFER_COUNT) ||
        !dma_alloc(&samples, AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT * 4u)) {
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
    stream_active = false;
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

bool audio_stream_start(const audio_format_t *format)
{
    if (!available || !format || format->channels != 2 ||
        format->bits_per_sample != 16 || format->sample_rate < 8000 ||
        format->sample_rate > 48000)
        return false;
    audio_stream_stop();
    ac97_mixer_write(AC97_PCM_RATE, (uint16_t)format->sample_rate);
    queued_frames = 0;
    played_frames = 0;
    active_buffer = 0;
    stream_active = true;
    stream_paused = false;
    return true;
}

size_t audio_stream_write(const int16_t *input, size_t frames)
{
    if (!stream_active || !input || frames == 0)
        return 0;
    size_t capacity = AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT - queued_frames;
    if (frames > capacity)
        frames = capacity;
    int16_t *output = (int16_t *)samples.virt;
    size_t write_at = (played_frames + queued_frames) %
                      (AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT);
    for (size_t i = 0; i < frames; i++) {
        output[(write_at + i) * 2] = input[i * 2];
        output[(write_at + i) * 2 + 1] = input[i * 2 + 1];
    }
    queued_frames += (uint32_t)frames;
    return frames;
}

static void ac97_start_buffer(uint8_t index)
{
    ac97_bdle_t *bdle = (ac97_bdle_t *)descriptors.virt;
    bdle[index].address = (uint32_t)(samples.phys +
        index * AC97_BUFFER_FRAMES * 4u);
    bdle[index].length = (uint16_t)(AC97_BUFFER_FRAMES * 4u);
    bdle[index].flags = 0x8000;
    outl((uint16_t)(bus_master_base + AC97_PO_BDBAR),
         (uint32_t)descriptors.phys);
    outw((uint16_t)(bus_master_base + AC97_PO_SR), 0x1c);
    outb((uint16_t)(bus_master_base + AC97_PO_LVI), index);
    outb((uint16_t)(bus_master_base + AC97_PO_CR), 0x01);
}

void audio_poll(void)
{
    if (!stream_active || stream_paused || queued_frames < AC97_BUFFER_FRAMES)
        return;
    uint8_t civ = inb((uint16_t)(bus_master_base + AC97_PO_CIV));
    uint8_t next = (uint8_t)((civ + 1) % AC97_BUFFER_COUNT);
    if (next != active_buffer && queued_frames >= AC97_BUFFER_FRAMES) {
        played_frames += AC97_BUFFER_FRAMES;
        queued_frames -= AC97_BUFFER_FRAMES;
        active_buffer = next;
        ac97_start_buffer(active_buffer);
    } else if (!inb((uint16_t)(bus_master_base + AC97_PO_CR))) {
        ac97_start_buffer(active_buffer);
    }
}

void audio_stream_pause(bool paused)
{
    stream_paused = paused;
    if (available && stream_active)
        outb((uint16_t)(bus_master_base + AC97_PO_CR), paused ? 0 : 1);
}

void audio_stream_stop(void)
{
    if (available)
        outb((uint16_t)(bus_master_base + AC97_PO_CR), 0);
    stream_active = false;
    stream_paused = false;
    queued_frames = 0;
}

size_t audio_stream_queued_frames(void) { return queued_frames; }
uint64_t audio_stream_position(void) { return played_frames; }

bool audio_play_doremi(void)
{
    static const uint16_t notes[] = { 262, 294, 330, 349, 392, 440, 494, 523 };
    audio_format_t format = { AC97_RATE, 2, 16 };
    int16_t buffer[128 * 2];
    if (!audio_stream_start(&format)) return false;
    for (size_t note = 0; note < ARRAY_SIZE(notes); note++) {
        uint32_t period = AC97_RATE / notes[note];
        for (uint32_t sent = 0; sent < AC97_RATE / 4;) {
            size_t count = MIN((uint32_t)ARRAY_SIZE(buffer) / 2,
                               AC97_RATE / 4 - sent);
            for (size_t i = 0; i < count; i++) {
                int16_t value = ((sent + i) % period < period / 2) ? 10000 : -10000;
                buffer[i * 2] = value; buffer[i * 2 + 1] = value;
            }
            size_t written = audio_stream_write(buffer, count);
            if (!written) { audio_poll(); timer_sleep(2); continue; }
            sent += (uint32_t)written;
            while (audio_stream_queued_frames() > AC97_BUFFER_FRAMES * 2) {
                audio_poll(); timer_sleep(2);
            }
        }
    }
    while (audio_stream_queued_frames()) { audio_poll(); timer_sleep(2); }
    audio_stream_stop();
    return true;
}
