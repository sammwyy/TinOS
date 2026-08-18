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
#define STBI_MALLOC(size) kmalloc(size)
#define STBI_REALLOC(pointer, size) krealloc(pointer, size)
#define STBI_FREE(pointer) kfree(pointer)
#define STBI_ASSERT(condition) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb/stb_image.h"
