#ifndef TINOS_UAPI_DESKTOP_H
#define TINOS_UAPI_DESKTOP_H

#include <stdint.h>

/* Version 1: process-owned window handles and copied text-cell frames.
 * Version 2: bounded tiled XRGB8888 surfaces and the system color theme.
 * All fields have fixed widths; no kernel pointers cross this ABI. */
#define TINOS_GUI_COLS 80u
#define TINOS_GUI_ROWS 32u
#define TINOS_GUI_TEXT_SIZE (TINOS_GUI_COLS * TINOS_GUI_ROWS)
#define TINOS_GUI_IMAGE_WIDTH 320u
#define TINOS_GUI_IMAGE_HEIGHT 192u
#define TINOS_GUI_SURFACE_MAX_WIDTH 512u
#define TINOS_GUI_SURFACE_MAX_HEIGHT 320u
#define TINOS_GUI_TILE_MAX_WIDTH 64u
#define TINOS_GUI_TILE_MAX_HEIGHT 64u

#define TINOS_GUI_KEY    1u
#define TINOS_GUI_CLOSE  2u
#define TINOS_GUI_RESIZE 3u
#define TINOS_GUI_MOUSE  4u
#define TINOS_GUI_OUTPUT 5u
#define TINOS_GUI_CHILD_EXIT 6u
#define TINOS_GUI_TIMER  7u

#define TINOS_KEY_BACKSPACE 8u
#define TINOS_KEY_TAB 9u
#define TINOS_KEY_ENTER 10u
#define TINOS_KEY_ESCAPE 27u
#define TINOS_KEY_UP 0x100u
#define TINOS_KEY_DOWN 0x101u
#define TINOS_KEY_LEFT 0x102u
#define TINOS_KEY_RIGHT 0x103u
#define TINOS_KEY_PAGEUP 0x106u
#define TINOS_KEY_PAGEDOWN 0x107u
#define TINOS_KEY_DELETE 0x109u
#define TINOS_MOD_CTRL 0x02u

typedef struct {
    uint32_t type;
    uint32_t code; /* KEY: keycode; MOUSE: pressed button mask */
    uint32_t ch;   /* printable ASCII character, or zero */
    uint32_t mods;
    int32_t x;     /* RESIZE: columns; MOUSE: client-relative x */
    int32_t y;     /* RESIZE: rows; MOUSE: client-relative y */
} tinos_gui_event_t;

typedef struct {
    uint32_t width;
    uint32_t height;
} tinos_gui_size_t;

/* The compositor copies one tightly packed XRGB8888 tile from user memory. */
typedef struct {
    uint32_t handle;
    uint32_t x, y;
    uint32_t width, height;
    uint32_t pixels;
} tinos_gui_tile_t;

typedef struct {
    uint32_t revision;
    uint32_t background, surface, foreground, muted;
    uint32_t accent, accent_text, border, selection, danger;
} tinos_gui_theme_t;

#define TINOS_DIRENT_FILE 1u
#define TINOS_DIRENT_DIR  2u

typedef struct {
    char name[64];
    uint32_t type;
    uint32_t size;
} tinos_dirent_t;

typedef struct {
    uint32_t uptime_ms;
    uint32_t memory_total_kib;
    uint32_t memory_free_kib;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t fs_nodes;
    char cpu[64];
    char platform[48];
    char clock[32];
} tinos_system_info_t;

#endif
