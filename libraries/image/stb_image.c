#include <tinos/heap.h>
#include <tinos/kernel.h>

#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_TGA
#define STBI_NO_BMP
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_TINOS
/* stb_image's own hand-written SSE2 path pulls in GCC's <emmintrin.h>,
 * which (via <mm_malloc.h>) assumes a hosted libc we don't have. The
 * surrounding scalar C still gets vectorized by -ftree-vectorize, so
 * nothing is lost by keeping stb_image's manual intrinsics off. */
#define STBI_NO_SIMD
#define STBI_MALLOC(size) kmalloc(size)
#define STBI_REALLOC(pointer, size) krealloc(pointer, size)
#define STBI_FREE(pointer) kfree(pointer)
#define STBI_ASSERT(condition) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"
