#ifndef TINOS_UAPI_DESKTOP_H
#define TINOS_UAPI_DESKTOP_H

#include <stdint.h>

/* Version 1: opaque per-process window handle and copied text-cell frames.
 * All fields have fixed widths so the ABI does not expose kernel pointers. */
#define TINOS_GUI_COLS 80u
#define TINOS_GUI_ROWS 32u
#define TINOS_GUI_TEXT_SIZE (TINOS_GUI_COLS * TINOS_GUI_ROWS)

#define TINOS_GUI_KEY    1u
#define TINOS_GUI_CLOSE  2u
#define TINOS_GUI_RESIZE 3u
#define TINOS_GUI_MOUSE  4u
#define TINOS_GUI_OUTPUT 5u
#define TINOS_GUI_CHILD_EXIT 6u

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
