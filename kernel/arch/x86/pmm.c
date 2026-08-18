/* Physical page frame allocator.
 *
 * TinOS runs with paging disabled, so "physical" and "virtual" addresses are
 * the same thing.  The allocator is a straightforward bitmap over the first
 * 4 GiB of address space: every frame starts out reserved and only the
 * regions the bootloader reports as available are released, after which the
 * areas we must not hand out (low memory, the kernel image, the multiboot
 * information block) are reserved again.
 */

#include <tinos/kernel.h>
#include <tinos/mm.h>
#include <tinos/spinlock.h>
#include <tinos/string.h>

#define MAX_FRAMES  (1u << 20) /* 4 GiB / 4 KiB */
#define BITMAP_WORDS (MAX_FRAMES / 32)

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

static uint32_t bitmap[BITMAP_WORDS];
static uint64_t total_bytes;
static uint64_t used_frames;
static uint64_t managed_frames;
static size_t   regions;
static size_t   search_hint;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static inline bool frame_used(size_t idx)
{
    return (bitmap[idx / 32] & (1u << (idx % 32))) != 0;
}

static inline void frame_mark_used(size_t idx)
{
    if (!frame_used(idx)) {
        bitmap[idx / 32] |= (1u << (idx % 32));
        used_frames++;
    }
}

static inline void frame_mark_free(size_t idx)
{
    if (frame_used(idx)) {
        bitmap[idx / 32] &= ~(1u << (idx % 32));
        used_frames--;
    }
}

static void reserve_range_locked(uintptr_t base, size_t length)
{
    if (length == 0)
        return;
    size_t first = base / PAGE_SIZE;
    size_t last  = (base + length - 1) / PAGE_SIZE;
    for (size_t i = first; i <= last && i < MAX_FRAMES; i++)
        frame_mark_used(i);
}

void pmm_reserve_range(uintptr_t base, size_t length)
{
    bool saved = spin_lock_irqsave(&pmm_lock);
    reserve_range_locked(base, length);
    spin_unlock_irqrestore(&pmm_lock, saved);
}

static void release_range(uint64_t base, uint64_t length)
{
    uint64_t start = ALIGN_UP(base, PAGE_SIZE);
    uint64_t end   = ALIGN_DOWN(base + length, PAGE_SIZE);

    if (end <= start)
        return;
    if (start >= (uint64_t)MAX_FRAMES * PAGE_SIZE)
        return;
    if (end > (uint64_t)MAX_FRAMES * PAGE_SIZE)
        end = (uint64_t)MAX_FRAMES * PAGE_SIZE;

    for (uint64_t addr = start; addr < end; addr += PAGE_SIZE) {
        size_t idx = (size_t)(addr / PAGE_SIZE);
        if (frame_used(idx))
            managed_frames++;
        frame_mark_free(idx);
    }
}

void pmm_init(const mb_info_t *mbi)
{
    memset(bitmap, 0xFF, sizeof(bitmap)); /* everything reserved */
    used_frames    = MAX_FRAMES;
    managed_frames = 0;
    total_bytes    = 0;
    regions        = 0;

    const mb_tag_mmap_t *mmap = NULL;
    MB_FOR_EACH_TAG(mbi, tag)
    {
        if (tag->type == MB_TAG_MMAP)
            mmap = (const mb_tag_mmap_t *)tag;
    }

    if (!mmap)
        panic("multiboot2: no memory map provided by the bootloader");

    size_t count = (mmap->size - sizeof(mb_tag_mmap_t)) / mmap->entry_size;
    for (size_t i = 0; i < count; i++) {
        const mb_mmap_entry_t *e =
            (const mb_mmap_entry_t *)((uintptr_t)mmap->entries +
                                      i * mmap->entry_size);
        if (e->type != MB_MEMORY_AVAILABLE)
            continue;
        regions++;
        total_bytes += e->len;
        release_range(e->addr, e->len);
    }

    /* Never hand out the first megabyte (BIOS data, VGA, EBDA), the kernel
     * image itself, or the multiboot information block. */
    pmm_reserve_range(0, 0x100000);
    pmm_reserve_range((uintptr_t)__kernel_start,
                      (size_t)(__kernel_end - __kernel_start));
    const mb_info_t *info = mbi;
    pmm_reserve_range((uintptr_t)info, info->total_size);

    search_hint = 0x100000 / PAGE_SIZE;
}

void *pmm_alloc_frame(void)
{
    return pmm_alloc_contig(1);
}

void *pmm_alloc_contig(size_t frames)
{
    if (frames == 0)
        return NULL;

    bool saved = spin_lock_irqsave(&pmm_lock);

    size_t run   = 0;
    size_t start = 0;
    void  *result = NULL;

    for (size_t pass = 0; pass < 2 && !result; pass++) {
        size_t begin = pass == 0 ? search_hint : 0;
        size_t end   = pass == 0 ? MAX_FRAMES : search_hint;
        run          = 0;
        for (size_t i = begin; i < end; i++) {
            /* Skip fully used words quickly. */
            if ((i % 32) == 0 && bitmap[i / 32] == 0xFFFFFFFFu) {
                run = 0;
                i += 31;
                continue;
            }
            if (frame_used(i)) {
                run = 0;
                continue;
            }
            if (run == 0)
                start = i;
            if (++run == frames) {
                for (size_t j = start; j < start + frames; j++)
                    frame_mark_used(j);
                search_hint = start + frames;
                result = (void *)(uintptr_t)(start * PAGE_SIZE);
                break;
            }
        }
    }

    spin_unlock_irqrestore(&pmm_lock, saved);
    return result; /* NULL: out of physical memory */
}

void pmm_free_frame(void *frame)
{
    pmm_free_contig(frame, 1);
}

void pmm_free_contig(void *base, size_t frames)
{
    uintptr_t addr = (uintptr_t)base;
    if (addr == 0 || (addr % PAGE_SIZE) != 0)
        return;

    bool saved = spin_lock_irqsave(&pmm_lock);
    size_t first = addr / PAGE_SIZE;
    for (size_t i = 0; i < frames && first + i < MAX_FRAMES; i++)
        frame_mark_free(first + i);
    if (first < search_hint)
        search_hint = first;
    spin_unlock_irqrestore(&pmm_lock, saved);
}

uint64_t pmm_total_bytes(void)
{
    return total_bytes;
}

uint64_t pmm_used_bytes(void)
{
    uint64_t unmanaged = MAX_FRAMES - managed_frames;
    uint64_t used      = used_frames > unmanaged ? used_frames - unmanaged : 0;
    return used * PAGE_SIZE;
}

uint64_t pmm_free_bytes(void)
{
    uint64_t managed = managed_frames * (uint64_t)PAGE_SIZE;
    uint64_t used    = pmm_used_bytes();
    return managed > used ? managed - used : 0;
}

size_t pmm_region_count(void)
{
    return regions;
}
