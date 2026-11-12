#include <tinos/audio.h>
#include <tinos/arch.h>
#include <tinos/bus.h>
#include <tinos/dma.h>
#include <tinos/driver.h>
#include <tinos/io.h>
#include <tinos/kernel.h>
#include <tinos/pci.h>
#include <tinos/spinlock.h>
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
#define AC97_PO_PICB     0x18
#define AC97_PO_CR       0x1b
#define AC97_GLOB_CNT    0x2c

#define AC97_SR_DCH      0x01
#define AC97_SR_LVBCI    0x04
#define AC97_SR_BCIS     0x08
#define AC97_SR_FIFOE    0x10
#define AC97_CR_RUN      0x01
#define AC97_CR_RESET    0x02

#define AC97_RATE        22050u
#define AC97_BUFFER_FRAMES 2048u
#define AC97_BUFFER_COUNT 32u

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
static uint32_t submitted_frames;
static uint32_t descriptor_frames[AC97_BUFFER_COUNT];
static uint8_t current_descriptor;
static uint8_t last_valid_descriptor;
static uint8_t in_flight;
static bool stream_active;
static bool stream_paused;
static bool stream_draining;
static bool dma_running;
static uint8_t volume = 80;
static bool muted;
/* Guards every field above from queued_frames down through dma_running:
 * the desktop's own tick and a (formerly boot-processor-only) playback
 * worker both call audio_poll()/audio_stream_write(), and now that
 * worker is free to run on any SMP core, irq_save() alone (which only
 * ever stopped the calling core's own interrupts) is not enough. */
static spinlock_t audio_lock = SPINLOCK_INIT;

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

void audio_set_volume(uint8_t value)
{
    volume = value > 100 ? 100 : value;
    if (!available)
        return;
    uint16_t attenuation = (uint16_t)((100 - volume) * 31 / 100);
    ac97_mixer_write(AC97_MASTER_VOL,
                     (uint16_t)(attenuation | (attenuation << 8) |
                                (muted ? 0x8000 : 0)));
    bus_publish(&(bus_event_t){ BUS_AUDIO_CHANGED, volume, muted, 0 });
}

uint8_t audio_get_volume(void) { return volume; }

void audio_set_muted(bool value)
{
    muted = value;
    audio_set_volume(volume);
}

bool audio_is_muted(void) { return muted; }

static void ac97_reset_dma(void);

/* Callers below must hold audio_lock. */
static void audio_stream_stop_locked(void)
{
    if (available)
        ac97_reset_dma();
    stream_active = false;
    stream_paused = false;
    stream_draining = false;
    dma_running = false;
    queued_frames = 0;
    submitted_frames = 0;
    in_flight = 0;
}

bool audio_stream_start(const audio_format_t *format)
{
    if (!available || !format || format->channels != 2 ||
        format->bits_per_sample != 16 || format->sample_rate < 8000 ||
        format->sample_rate > 48000)
        return false;

    bool saved = spin_lock_irqsave(&audio_lock);
    audio_stream_stop_locked();
    ac97_mixer_write(AC97_PCM_RATE, (uint16_t)format->sample_rate);
    queued_frames = 0;
    played_frames = 0;
    submitted_frames = 0;
    current_descriptor = 0;
    last_valid_descriptor = 0;
    in_flight = 0;
    stream_active = true;
    stream_paused = false;
    stream_draining = false;
    dma_running = false;
    spin_unlock_irqrestore(&audio_lock, saved);
    return true;
}

size_t audio_stream_write(const int16_t *input, size_t frames)
{
    if (!input || frames == 0)
        return 0;

    bool saved = spin_lock_irqsave(&audio_lock);
    if (!stream_active) {
        spin_unlock_irqrestore(&audio_lock, saved);
        return 0;
    }
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
    spin_unlock_irqrestore(&audio_lock, saved);
    return frames;
}

static void ac97_prepare_descriptor(uint8_t descriptor, uint32_t offset,
                                    uint32_t frames)
{
    ac97_bdle_t *bdle = (ac97_bdle_t *)descriptors.virt;
    bdle[descriptor].address = (uint32_t)(samples.phys + offset * 4u);
    bdle[descriptor].length = (uint16_t)(frames * 2u);
    bdle[descriptor].flags = 0x8000;
    descriptor_frames[descriptor] = frames;
}

static void ac97_reset_dma(void)
{
    outb((uint16_t)(bus_master_base + AC97_PO_CR), 0);
    for (uint32_t i = 0; i < 100000; i++) {
        if (inw((uint16_t)(bus_master_base + AC97_PO_SR)) & AC97_SR_DCH)
            break;
        io_wait();
    }
    outb((uint16_t)(bus_master_base + AC97_PO_CR), AC97_CR_RESET);
    for (uint32_t i = 0; i < 100000; i++) {
        if (!(inb((uint16_t)(bus_master_base + AC97_PO_CR)) & AC97_CR_RESET))
            break;
        io_wait();
    }
}

static uint32_t ac97_next_submission(void)
{
    uint32_t available_frames = queued_frames - submitted_frames;
    if (available_frames >= AC97_BUFFER_FRAMES)
        return AC97_BUFFER_FRAMES;
    return stream_draining ? available_frames : 0;
}

static void ac97_start_dma(void)
{
    uint32_t frames = ac97_next_submission();
    if (!frames || stream_paused)
        return;

    current_descriptor = 0;
    last_valid_descriptor = 0;
    in_flight = 0;
    while (frames && in_flight < AC97_BUFFER_COUNT) {
        uint32_t offset = (uint32_t)((played_frames + submitted_frames) %
                          (AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT));
        ac97_prepare_descriptor(last_valid_descriptor, offset, frames);
        submitted_frames += frames;
        in_flight++;
        frames = ac97_next_submission();
        if (frames && in_flight < AC97_BUFFER_COUNT)
            last_valid_descriptor = (uint8_t)(last_valid_descriptor + 1);
    }

    outl((uint16_t)(bus_master_base + AC97_PO_BDBAR),
         (uint32_t)descriptors.phys);
    outw((uint16_t)(bus_master_base + AC97_PO_SR),
         AC97_SR_LVBCI | AC97_SR_BCIS | AC97_SR_FIFOE);
    outb((uint16_t)(bus_master_base + AC97_PO_LVI), last_valid_descriptor);
    outb((uint16_t)(bus_master_base + AC97_PO_CR), AC97_CR_RUN);
    dma_running = true;
}

static void ac97_complete_descriptor(uint8_t descriptor)
{
    uint32_t frames = descriptor_frames[descriptor];
    if (frames > queued_frames || frames > submitted_frames)
        frames = MIN(queued_frames, submitted_frames);
    played_frames += frames;
    queued_frames -= frames;
    submitted_frames -= frames;
    descriptor_frames[descriptor] = 0;
    if (in_flight)
        in_flight--;
}

static void ac97_extend_dma(void)
{
    uint32_t frames = ac97_next_submission();
    while (frames && in_flight < AC97_BUFFER_COUNT) {
        uint8_t descriptor = (uint8_t)((last_valid_descriptor + 1) %
                                       AC97_BUFFER_COUNT);
        uint32_t offset = (uint32_t)((played_frames + submitted_frames) %
                          (AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT));
        ac97_prepare_descriptor(descriptor, offset, frames);
        submitted_frames += frames;
        in_flight++;
        last_valid_descriptor = descriptor;
        outb((uint16_t)(bus_master_base + AC97_PO_LVI), descriptor);
        frames = ac97_next_submission();
    }
}

/* Caller must hold audio_lock. */
static void audio_poll_locked(void)
{
    if (!stream_active || stream_paused)
        return;

    if (!dma_running) {
        ac97_start_dma();
        return;
    }

    uint16_t status = inw((uint16_t)(bus_master_base + AC97_PO_SR));
    uint8_t civ = inb((uint16_t)(bus_master_base + AC97_PO_CIV));
    while (current_descriptor != civ && in_flight) {
        ac97_complete_descriptor(current_descriptor);
        current_descriptor = (uint8_t)((current_descriptor + 1) %
                                       AC97_BUFFER_COUNT);
    }

    uint16_t remaining = inw((uint16_t)(bus_master_base + AC97_PO_PICB));
    if ((status & AC97_SR_DCH) && (status & AC97_SR_LVBCI) &&
        remaining == 0 && in_flight) {
        while (in_flight) {
            ac97_complete_descriptor(current_descriptor);
            current_descriptor = (uint8_t)((current_descriptor + 1) %
                                           AC97_BUFFER_COUNT);
        }
        dma_running = false;
        ac97_reset_dma();
    }

    if (status & (AC97_SR_LVBCI | AC97_SR_BCIS | AC97_SR_FIFOE))
        outw((uint16_t)(bus_master_base + AC97_PO_SR),
             status & (AC97_SR_LVBCI | AC97_SR_BCIS | AC97_SR_FIFOE));

    if (dma_running)
        ac97_extend_dma();
    else
        ac97_start_dma();
}

void audio_poll(void)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    audio_poll_locked();
    spin_unlock_irqrestore(&audio_lock, saved);
}

void audio_stream_pause(bool paused)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    stream_paused = paused;
    if (available && stream_active)
        outb((uint16_t)(bus_master_base + AC97_PO_CR), paused ? 0 : 1);
    spin_unlock_irqrestore(&audio_lock, saved);
}

void audio_stream_stop(void)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    audio_stream_stop_locked();
    spin_unlock_irqrestore(&audio_lock, saved);
}

size_t audio_stream_queued_frames(void)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    size_t queued = queued_frames;
    spin_unlock_irqrestore(&audio_lock, saved);
    return queued;
}
size_t audio_stream_capacity_frames(void)
{
    return AC97_BUFFER_FRAMES * AC97_BUFFER_COUNT;
}
uint64_t audio_stream_position(void)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    uint64_t position = played_frames;
    spin_unlock_irqrestore(&audio_lock, saved);
    return position;
}

void audio_stream_drain(void)
{
    bool saved = spin_lock_irqsave(&audio_lock);
    if (stream_active) {
        stream_draining = true;
        audio_poll_locked();
    }
    spin_unlock_irqrestore(&audio_lock, saved);
}

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
    audio_stream_drain();
    while (audio_stream_queued_frames()) { audio_poll(); timer_sleep(2); }
    audio_stream_stop();
    return true;
}
