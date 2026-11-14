/* PS/2 keyboard driver (scancode set 1).
 *
 * The interrupt handler translates make codes into key events and pushes
 * them into a ring buffer that the desktop event loop drains.  Break codes
 * only update the modifier state.  The 0xE0 prefix selects the extended
 * table used by the arrow/navigation keys.
 */

#include <tinos/input.h>
#include <tinos/arch.h>
#include <tinos/bus.h>
#include <tinos/io.h>
#include <tinos/kernel.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

#define QUEUE_SIZE 64

static volatile key_event_t queue[QUEUE_SIZE];
static volatile uint32_t    q_head, q_tail;
static uint8_t              mods;
static bool                 extended;
static bool                 super_combo; /* a key was pressed while Win was held */
static keyboard_layout_t    layout = KEYBOARD_LAYOUT_US;

/* Unshifted characters for scancode set 1, index = make code. */
static const char keymap[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',  /* 00-07 */
    '7',  '8', '9', '0', '-', '=', '\b', '\t',/* 08-0F */
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',  /* 10-17 */
    'o',  'p', '[', ']', '\n', 0,  'a', 's',  /* 18-1F */
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',  /* 20-27 */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', /* 28-2F */
    'b',  'n', 'm', ',', '.', '/', 0,   '*',  /* 30-37 */
    0,    ' ', 0,   0,   0,   0,   0,   0,    /* 38-3F */
    0,    0,   0,   0,   0,   0,   0,   '7',  /* 40-47 keypad */
    '8',  '9', '-', '4', '5', '6', '+', '1',  /* 48-4F */
    '2',  '3', '0', '.', 0,   0,   0,   0,    /* 50-57 */
};

static const char keymap_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',
    '&',  '*', '(', ')', '_', '+', '\b', '\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P', '{', '}', '\n', 0,  'A', 'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"',  '~', 0,   '|', 'Z', 'X', 'C', 'V',
    'B',  'N', 'M', '<', '>', '?', 0,   '*',
    0,    ' ', 0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   '7',
    '8',  '9', '-', '4', '5', '6', '+', '1',
    '2',  '3', '0', '.', 0,   0,   0,   0,
};

/* Latin Spanish keeps the alphanumeric block unchanged and maps the
 * punctuation positions commonly used by the PS/2 Spanish layout. */
static const char keymap_es[128] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\'', '?',
    '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '`', '+',
    '\n', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'n', '\'', 'c',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-', 0, '*', 0, ' ', 0, 0,
};
static const char keymap_es_shift[128] = {
    0, 0, '!', '"', '#', '$', '%', '&', '/', '(', ')', '=', '?', '?',
    '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '^', '*',
    '\n', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'N', '"', 'C',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', ';', ':', '_', 0, '*', 0, ' ', 0, 0,
};

void keyboard_set_layout(keyboard_layout_t value)
{
    layout = value;
    bus_publish(&(bus_event_t){ BUS_KEYBOARD_CHANGED, value, 0, 0 });
}
keyboard_layout_t keyboard_get_layout(void) { return layout; }

static void push_event(uint16_t code, char ch)
{
    uint32_t next = (q_head + 1) % QUEUE_SIZE;
    if (next == q_tail)
        return; /* queue full: drop the oldest input rather than block */
    queue[q_head].code = code;
    queue[q_head].ch   = ch;
    queue[q_head].mods = mods;
    q_head             = next;
    if ((mods & KMOD_SUPER) && code != KEY_SUPER)
        super_combo = true;
}

static uint16_t extended_key(uint8_t code)
{
    switch (code) {
    case 0x48: return KEY_UP;
    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;
    case 0x4F: return KEY_END;
    case 0x49: return KEY_PAGEUP;
    case 0x51: return KEY_PAGEDOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x1C: return KEY_ENTER;  /* keypad enter */
    case 0x35: return '/';        /* keypad slash */
    case 0x5B:
    case 0x5C: return KEY_SUPER;
    default:   return KEY_NONE;
    }
}

static uint16_t function_key(uint8_t code)
{
    if (code >= 0x3B && code <= 0x44)
        return (uint16_t)(KEY_F1 + (code - 0x3B));
    if (code == 0x57)
        return KEY_F11;
    if (code == 0x58)
        return KEY_F12;
    return KEY_NONE;
}

static void keyboard_isr(regs_t *regs)
{
    UNUSED(regs);

    uint8_t scancode = inb(KBD_DATA);

    if (scancode == 0xE0) {
        extended = true;
        return;
    }

    bool    released = (scancode & 0x80) != 0;
    uint8_t code     = scancode & 0x7F;

    if (extended) {
        extended = false;
        switch (code) {
        case 0x1D: /* right control */
            if (released)
                mods &= (uint8_t)~KMOD_CTRL;
            else
                mods |= KMOD_CTRL;
            return;
        case 0x38: /* right alt */
            if (released)
                mods &= (uint8_t)~KMOD_ALT;
            else
                mods |= KMOD_ALT;
            return;
        case 0x5B: /* left/right Windows key */
        case 0x5C:
            /* A lone tap is KEY_SUPER (the start menu); held, it is a
             * modifier for Win+<key> shortcuts. */
            if (!released) {
                if (!(mods & KMOD_SUPER))
                    super_combo = false;
                mods |= KMOD_SUPER;
            } else {
                mods &= (uint8_t)~KMOD_SUPER;
                if (!super_combo)
                    push_event(KEY_SUPER, 0);
            }
            return;
        default:
            break;
        }
        if (!released) {
            uint16_t key = extended_key(code);
            if (key != KEY_NONE)
                push_event(key, key == '/' ? '/' : 0);
        }
        return;
    }

    switch (code) {
    case 0x2A: /* left shift  */
    case 0x36: /* right shift */
        if (released)
            mods &= (uint8_t)~KMOD_SHIFT;
        else
            mods |= KMOD_SHIFT;
        return;
    case 0x1D: /* left control */
        if (released)
            mods &= (uint8_t)~KMOD_CTRL;
        else
            mods |= KMOD_CTRL;
        return;
    case 0x38: /* left alt */
        if (released)
            mods &= (uint8_t)~KMOD_ALT;
        else
            mods |= KMOD_ALT;
        return;
    case 0x3A: /* caps lock */
        if (!released)
            mods ^= KMOD_CAPS;
        return;
    default:
        break;
    }

    if (released)
        return;

    uint16_t fkey = function_key(code);
    if (fkey != KEY_NONE) {
        push_event(fkey, 0);
        return;
    }
    if (code == 0x01) {
        push_event(KEY_ESCAPE, 0);
        return;
    }

    const char *normal = layout == KEYBOARD_LAYOUT_ES ? keymap_es : keymap;
    const char *shifted = layout == KEYBOARD_LAYOUT_ES ? keymap_es_shift :
                                                        keymap_shift;
    char ch = (mods & KMOD_SHIFT) ? shifted[code] : normal[code];
    if (!ch)
        return;

    /* Caps lock inverts the case of letters only. */
    if (mods & KMOD_CAPS) {
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 32);
        else if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + 32);
    }

    switch (ch) {
    case '\b':
        push_event(KEY_BACKSPACE, 0);
        return;
    case '\t':
        push_event(KEY_TAB, 0);
        return;
    case '\n':
        push_event(KEY_ENTER, 0);
        return;
    default:
        push_event((uint16_t)(uint8_t)ch, ch);
        return;
    }
}

void keyboard_init(void)
{
    q_head = q_tail = 0;
    mods            = 0;
    extended        = false;

    /* Drain anything the firmware left behind. */
    for (int i = 0; i < 32 && (inb(KBD_STATUS) & 0x01); i++)
        (void)inb(KBD_DATA);

    irq_install(1, keyboard_isr);
}

bool keyboard_poll(key_event_t *out)
{
    bool saved = irq_save();
    bool got   = false;
    if (q_tail != q_head) {
        *out   = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE_SIZE;
        got    = true;
    }
    irq_restore(saved);
    return got;
}

uint8_t keyboard_mods(void)
{
    return mods;
}
