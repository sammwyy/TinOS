/* Simple identity-mapped DMA allocator.
 * Keeping the physical-address assumption behind this API lets a future VM
 * layer change mappings without changing PCI drivers. */

#include <tinos/dma.h>
#include <tinos/string.h>

bool dma_alloc(dma_buffer_t *buffer, size_t size)
{
    if (!buffer || size == 0)
        return false;
    size_t frames = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    void *memory = pmm_alloc_contig(frames);
    if (!memory)
        return false;
    buffer->virt = memory;
    buffer->phys = (uintptr_t)memory;
    buffer->size = size;
    buffer->frames = frames;
    return true;
}

void dma_free(dma_buffer_t *buffer)
{
    if (!buffer || !buffer->virt)
        return;
    pmm_free_contig(buffer->virt, buffer->frames);
    memset(buffer, 0, sizeof(*buffer));
}

void dma_zero(dma_buffer_t *buffer)
{
    if (buffer && buffer->virt)
        memset(buffer->virt, 0, buffer->size);
}
