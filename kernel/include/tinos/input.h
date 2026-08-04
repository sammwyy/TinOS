/* Keyboard and mouse input. */
#ifndef TINOS_INPUT_H
#define TINOS_INPUT_H

#include <tinos/types.h>

/* Non-printable keys use codes above the ASCII range. */
enum {
    KEY_NONE      = 0,
    KEY_BACKSPACE = 8,
    KEY_TAB       = 9,
    KEY_ENTER     = 10,
    KEY_ESCAPE    = 27,

    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_SUPER,
    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12,
};

#define KMOD_SHIFT 0x01
#define KMOD_CTRL  0x02
#define KMOD_ALT   0x04
#define KMOD_CAPS  0x08

typedef struct {
    uint16_t code; /* KEY_* or the ASCII code of a printable key */
    char     ch;   /* printable character, or 0                  */
    uint8_t  mods;
} key_event_t;

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

typedef struct {
    int     x, y;
    int     dx, dy;
    int     wheel;    /* vertical scroll notches, positive = away from user */
    uint8_t buttons;  /* currently held    */
    uint8_t pressed;  /* newly pressed     */
    uint8_t released; /* newly released    */
    bool    moved;
} mouse_event_t;

void keyboard_init(void);
bool keyboard_poll(key_event_t *out);
uint8_t keyboard_mods(void);

bool mouse_init(void);
bool mouse_present(void);
bool mouse_poll(mouse_event_t *out);
void mouse_get_position(int *x, int *y);
uint8_t mouse_buttons(void);
void mouse_set_bounds(int width, int height);

#endif /* TINOS_INPUT_H */
