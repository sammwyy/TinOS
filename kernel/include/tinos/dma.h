/* DMA buffers backed by physically contiguous page frames. */
#ifndef TINOS_DMA_H
#define TINOS_DMA_H

#include <tinos/mm.h>
#include <tinos/types.h>

typedef struct {
    void *virt;
    uintptr_t phys;
    size_t size;
    size_t frames;
} dma_buffer_t;

bool dma_alloc(dma_buffer_t *buffer, size_t size);
void dma_free(dma_buffer_t *buffer);
void dma_zero(dma_buffer_t *buffer);

#endif /* TINOS_DMA_H */
