/* Terminal application.
 *
 * A scrollback buffer of wrapped text lines plus a line editor, driving the
 * shell in kernel/sh.  Everything the shell prints is appended to the
 * scrollback; requests it raises (clear, exit, open another application) are
 * carried out here.
 */

#include <tinos/arch.h>
#include <tinos/desktop.h>
#include <tinos/fs.h>
#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/kernel.h>
#include <tinos/net.h>
#include <tinos/path.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>
#include <tinos/ui.h>

#define INPUT_MAX      512
#define PADDING        6
#define BLINK_MS       500

typedef struct {
    shell_t *shell;

    char  **lines;
    size_t  line_count;
    size_t  line_cap;
    size_t scroll; /* index of the first visible line */

    char   input[INPUT_MAX];
    size_t input_len;
    size_t cursor;

    size_t history_pos; /* history_count means "editing a new line" */
    char   pending[INPUT_MAX];

    int      cols, rows;
    bool     blink_on;
    uint64_t blink_at;
} terminal_t;

/* Scrollback */

static void term_push_line(terminal_t *t, const char *text, size_t len)
{
    char *copy = kstrndup(text, len);
    if (!copy)
        return;
    if (t->line_count == t->line_cap) {
        size_t cap = t->line_cap ? t->line_cap * 2 : 256;
        char **grown = krealloc(t->lines, cap * sizeof(char *));
        if (!grown) {
            kfree(copy);
            return;
        }
        t->lines    = grown;
        t->line_cap = cap;
    }
    t->lines[t->line_count++] = copy;
}

/* Append text, splitting on newlines and wrapping at the window width. */
static void term_write(terminal_t *t, const char *text, size_t len)
{
    size_t start = 0;
    size_t cols  = (size_t)MAX(t->cols, 8);

    for (size_t i = 0; i <= len; i++) {
        bool eol = (i == len) || text[i] == '\n';
        if (!eol && i - start < cols)
            continue;
        if (!eol && i - start == cols) {
            term_push_line(t, text + start, cols);
            start = i;
            continue;
        }
        if (eol) {
            if (i == len && start == len)
                break;
            term_push_line(t, text + start, i - start);
            start = i + 1;
        }
    }
}

static void term_printf(terminal_t *t, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    term_write(t, buf, strlen(buf));
}

static void term_scroll_to_bottom(terminal_t *t)
{
    size_t visible = (size_t)MAX(t->rows - 1, 1);
    t->scroll = t->line_count > visible ? t->line_count - visible : 0;
}

static void term_clear(terminal_t *t)
{
    for (size_t i = 0; i < t->line_count; i++)
        kfree(t->lines[i]);
    t->line_count = 0;
    t->scroll     = 0;
}

/* Prompt and command execution */

static void term_prompt_text(terminal_t *t, char *buf, size_t size)
{
    ksnprintf(buf, size, "tinos:%s$ ", shell_cwd(t->shell));
}

static void term_update_title(window_t *win, terminal_t *t)
{
    char title[64];
    ksnprintf(title, sizeof(title), "Terminal - %s", shell_cwd(t->shell));
    wm_set_title(win, title);
}

static void term_run(window_t *win, terminal_t *t)
{
    char prompt[FS_PATH_MAX + 16];
    term_prompt_text(t, prompt, sizeof(prompt));

    char echoed[FS_PATH_MAX + INPUT_MAX + 16];
    ksnprintf(echoed, sizeof(echoed), "%s%s", prompt, t->input);
    term_write(t, echoed, strlen(echoed));

    stream_t out;
    stream_init(&out);
    shell_execute(t->shell, t->input, &out);
    if (out.size)
        term_write(t, out.data, out.size);
    stream_free(&out);

    /* Act on whatever the command asked the desktop to do. */
    char         arg[FS_PATH_MAX];
    sh_request_t request = shell_take_request(t->shell, arg, sizeof(arg));

    t->input[0]   = '\0';
    t->input_len  = 0;
    t->cursor     = 0;
    t->history_pos = shell_history_count(t->shell);
    term_update_title(win, t);
    term_scroll_to_bottom(t);

    switch (request) {
    case SH_REQ_CLEAR:
        term_clear(t);
        break;
    case SH_REQ_EXIT:
        wm_close(win); /* the window (and t) are gone after this */
        return;
    case SH_REQ_EXPLORER:
        app_explorer_open(arg);
        break;
    case SH_REQ_EDITOR:
        app_editor_open(arg);
        break;
    case SH_REQ_ABOUT:
        app_about_open(NULL);
        break;
    case SH_REQ_SYSINFO:
        app_sysinfo_open(NULL);
        break;
    case SH_REQ_REBOOT:
        desktop_shutdown(true);
        break;
    case SH_REQ_SHUTDOWN:
        desktop_shutdown(false);
        break;
    case SH_REQ_NONE:
        break;
    }
    wm_invalidate(win);
}

/* Line editing */

static void term_insert(terminal_t *t, char c)
{
    if (t->input_len + 1 >= INPUT_MAX)
        return;
    memmove(t->input + t->cursor + 1, t->input + t->cursor,
            t->input_len - t->cursor + 1);
    t->input[t->cursor++] = c;
    t->input_len++;
}

static void term_set_input(terminal_t *t, const char *text)
{
    strlcpy(t->input, text ? text : "", sizeof(t->input));
    t->input_len = strlen(t->input);
    t->cursor    = t->input_len;
}

static void term_history(terminal_t *t, int delta)
{
    size_t count = shell_history_count(t->shell);
    if (count == 0)
        return;

    if (delta < 0) {
        if (t->history_pos == 0)
            return;
        if (t->history_pos == count)
            strlcpy(t->pending, t->input, sizeof(t->pending));
        t->history_pos--;
        term_set_input(t, shell_history_at(t->shell, t->history_pos));
    } else {
        if (t->history_pos >= count)
            return;
        t->history_pos++;
        if (t->history_pos == count)
            term_set_input(t, t->pending);
        else
            term_set_input(t, shell_history_at(t->shell, t->history_pos));
    }
}

static void term_complete(terminal_t *t)
{
    /* Complete the word the cursor sits in. */
    size_t start = t->cursor;
    while (start > 0 && t->input[start - 1] != ' ')
        start--;

    char word[FS_PATH_MAX];
    size_t len = t->cursor - start;
    if (len >= sizeof(word))
        return;
    memcpy(word, t->input + start, len);
    word[len] = '\0';

    char completed[FS_PATH_MAX];
    if (!shell_complete(t->shell, word, completed, sizeof(completed)))
        return;

    char rest[INPUT_MAX];
    strlcpy(rest, t->input + t->cursor, sizeof(rest));
    char merged[INPUT_MAX];
    ksnprintf(merged, sizeof(merged), "%.*s%s%s", (int)start, t->input,
              completed, rest);
    size_t new_cursor = start + strlen(completed);
    term_set_input(t, merged);
    t->cursor = MIN(new_cursor, t->input_len);
}

/* Application class */

static void terminal_on_resize(window_t *win, rect_t client)
{
    terminal_t *t = win->priv;
    if (!t)
        return;
    t->cols = (client.w - 2 * PADDING) / FONT_W;
    t->rows = (client.h - 2 * PADDING) / FONT_H;
    term_scroll_to_bottom(t);
}

static void terminal_on_open(window_t *win, const char *arg)
{
    terminal_t *t = kcalloc(1, sizeof(terminal_t));
    if (!t) {
        klog("[term] out of memory\n");
        return;
    }
    win->priv = t;

    rect_t client = wm_client_rect(win);
    t->cols       = (client.w - 2 * PADDING) / FONT_W;
    t->rows       = (client.h - 2 * PADDING) / FONT_H;
    t->shell      = shell_create(arg && arg[0] ? arg : TINOS_DEFAULT_HOME);
    if (!t->shell) {
        klog("[term] could not create a shell\n");
        return;
    }
    t->history_pos = 0;

    term_printf(t, "%s %s - type 'help' for the list of commands", TINOS_NAME,
                TINOS_VERSION);
    term_printf(t, "");
    term_update_title(win, t);
    term_scroll_to_bottom(t);
}

static void terminal_on_close(window_t *win)
{
    terminal_t *t = win->priv;
    if (!t)
        return;
    term_clear(t);
    kfree(t->lines);
    shell_destroy(t->shell);
    kfree(t);
    win->priv = NULL;
}

static void terminal_on_draw(window_t *win, rect_t client)
{
    terminal_t *t = win->priv;
    if (!t)
        return;

    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_TERMINAL_BG);

    int x = client.x + PADDING;
    int y = client.y + PADDING;
    int visible_rows = t->rows - 1;

    for (int row = 0; row < visible_rows; row++) {
        size_t index = t->scroll + (size_t)row;
        if (index >= t->line_count)
            break;
        gfx_text(x, y + row * FONT_H, t->lines[index], COLOR_TERMINAL_TEXT);
    }

    /* The prompt follows the output until the view is full, then stays on
     * the last row. */
    char prompt[FS_PATH_MAX + 16];
    term_prompt_text(t, prompt, sizeof(prompt));
    size_t used_rows = t->line_count > t->scroll ? t->line_count - t->scroll : 0;
    int prompt_row = (int)MIN(used_rows, (size_t)visible_rows);
    int prompt_y   = y + prompt_row * FONT_H;
    int prompt_w   = gfx_text_width(prompt);

    gfx_text(x, prompt_y, prompt, COLOR_TERMINAL_PROMPT);

    /* Show the tail of the input if it is longer than the window. */
    int  avail  = (client.w - 2 * PADDING - prompt_w) / FONT_W;
    const char *shown = t->input;
    size_t      offset = 0;
    if (avail > 4 && (int)t->cursor >= avail) {
        offset = t->cursor - (size_t)avail + 1;
        shown  = t->input + offset;
    }
    gfx_text(x + prompt_w, prompt_y, shown, COLOR_TERMINAL_TEXT);

    if (t->blink_on && win == wm_focused()) {
        int cx = x + prompt_w + (int)(t->cursor - offset) * FONT_W;
        gfx_fill_rect(cx, prompt_y, 2, FONT_H, COLOR_TERMINAL_PROMPT);
    }

    /* Scrollback indicator. */
    size_t total = t->line_count;
    if (total > (size_t)visible_rows)
        widget_scrollbar(rect_make(client.x + client.w - 6, client.y, 6,
                                   client.h),
                         t->scroll, (size_t)visible_rows, total);
}

static bool terminal_on_key(window_t *win, const key_event_t *ev)
{
    terminal_t *t = win->priv;
    if (!t)
        return false;

    size_t page = (size_t)MAX(t->rows - 2, 1);

    if (ev->mods & KMOD_CTRL) {
        switch (ev->ch) {
        case 'l':
        case 'L':
            term_clear(t);
            wm_invalidate(win);
            return true;
        case 'u':
        case 'U':
            term_set_input(t, "");
            wm_invalidate(win);
            return true;
        case 'c':
        case 'C':
            term_printf(t, "tinos:%s$ %s^C", shell_cwd(t->shell), t->input);
            term_set_input(t, "");
            term_scroll_to_bottom(t);
            wm_invalidate(win);
            return true;
        default:
            break;
        }
    }

    switch (ev->code) {
    case KEY_ENTER:
        term_run(win, t);
        return true;
    case KEY_BACKSPACE:
        if (t->cursor > 0) {
            memmove(t->input + t->cursor - 1, t->input + t->cursor,
                    t->input_len - t->cursor + 1);
            t->cursor--;
            t->input_len--;
        }
        break;
    case KEY_DELETE:
        if (t->cursor < t->input_len) {
            memmove(t->input + t->cursor, t->input + t->cursor + 1,
                    t->input_len - t->cursor);
            t->input_len--;
        }
        break;
    case KEY_LEFT:
        if (t->cursor)
            t->cursor--;
        break;
    case KEY_RIGHT:
        if (t->cursor < t->input_len)
            t->cursor++;
        break;
    case KEY_HOME:
        t->cursor = 0;
        break;
    case KEY_END:
        t->cursor = t->input_len;
        break;
    case KEY_UP:
        term_history(t, -1);
        break;
    case KEY_DOWN:
        term_history(t, 1);
        break;
    case KEY_TAB:
        term_complete(t);
        break;
    case KEY_PAGEUP:
        t->scroll = t->scroll > page ? t->scroll - page : 0;
        break;
    case KEY_PAGEDOWN: {
        size_t visible = (size_t)MAX(t->rows - 1, 1);
        size_t max     = t->line_count > visible ? t->line_count - visible : 0;
        t->scroll      = MIN(t->scroll + page, max);
        break;
    }
    default:
        if (ev->ch >= 0x20 && ev->ch < 0x7F) {
            term_insert(t, ev->ch);
            term_scroll_to_bottom(t);
        } else {
            return false;
        }
        break;
    }

    t->blink_on = true;
    t->blink_at = 0;
    wm_invalidate(win);
    return true;
}

#define WHEEL_LINES 3

static bool terminal_on_mouse(window_t *win, const mouse_event_t *ev, int lx,
                              int ly)
{
    terminal_t *t = win->priv;
    if (!t || !ev->wheel)
        return true;

    /* PS/2 wheel Z-axis convention: negative = wheel pushed away from the
     * user ("scroll up" = show earlier lines), positive = toward the user. */
    size_t step = (size_t)(WHEEL_LINES * (ev->wheel > 0 ? ev->wheel : -ev->wheel));
    if (ev->wheel < 0) {
        t->scroll = t->scroll > step ? t->scroll - step : 0;
    } else {
        size_t visible = (size_t)MAX(t->rows - 1, 1);
        size_t max     = t->line_count > visible ? t->line_count - visible : 0;
        t->scroll      = MIN(t->scroll + step, max);
    }
    wm_invalidate(win);
    return true; /* clicks just focus the window */
}

static bool terminal_on_tick(window_t *win)
{
    terminal_t *t = win->priv;
    if (!t)
        return false;

    net_job_result_t result;
    bool network_output = false;
    while (net_async_take_result(&result)) {
        term_write(t, result.text, strlen(result.text));
        network_output = true;
    }
    if (network_output)
        term_scroll_to_bottom(t);

    uint64_t now = timer_ticks();
    if (now >= t->blink_at) {
        t->blink_on = !t->blink_on;
        t->blink_at = now + BLINK_MS;
        return win == wm_focused();
    }
    return network_output;
}

static const app_class_t terminal_class = {
    .name     = "terminal",
    .on_open  = terminal_on_open,
    .on_close = terminal_on_close,
    .on_draw  = terminal_on_draw,
    .on_key   = terminal_on_key,
    .on_mouse = terminal_on_mouse,
    .on_tick  = terminal_on_tick,
    .on_resize = terminal_on_resize,
};

window_t *app_terminal_open(const char *cwd)
{
    return wm_open(&terminal_class, 748, 452, "Terminal", cwd);
}
