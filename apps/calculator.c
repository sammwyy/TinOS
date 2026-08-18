#include <tinos/gfx.h>
#include <tinos/heap.h>
#include <tinos/printf.h>
#include <tinos/string.h>
#include <tinos/ui.h>

/* A deliberately small, integer calculator.  The parser lives here instead
 * of in the shell so the app remains useful even when no terminal is open. */
typedef struct {
    char expression[96];
    char display[64];
    bool just_evaluated;
} calculator_t;

typedef struct {
    const char *s;
    bool error;
} parser_t;

static void skip_space(parser_t *p)
{
    while (*p->s == ' ' || *p->s == '\t')
        p->s++;
}

static int64_t parse_expression(parser_t *p);

static int64_t parse_factor(parser_t *p)
{
    skip_space(p);
    bool negative = false;
    if (*p->s == '+' || *p->s == '-') {
        negative = *p->s++ == '-';
        skip_space(p);
    }

    int64_t value = 0;
    if (*p->s == '(') {
        p->s++;
        value = parse_expression(p);
        skip_space(p);
        if (*p->s != ')')
            p->error = true;
        else
            p->s++;
    } else if (*p->s >= '0' && *p->s <= '9') {
        while (*p->s >= '0' && *p->s <= '9')
            value = value * 10 + (*p->s++ - '0');
    } else {
        p->error = true;
    }
    return negative ? -value : value;
}

static int64_t parse_term(parser_t *p)
{
    int64_t value = parse_factor(p);
    for (;;) {
        skip_space(p);
        char op = *p->s;
        if (op != '*' && op != '/' && op != '%')
            return value;
        p->s++;
        int64_t rhs = parse_factor(p);
        if (op == '*')
            value *= rhs;
        else if (rhs == 0)
            p->error = true;
        else if (op == '/')
            value /= rhs;
        else
            value %= rhs;
    }
}

static int64_t parse_expression(parser_t *p)
{
    int64_t value = parse_term(p);
    for (;;) {
        skip_space(p);
        char op = *p->s;
        if (op != '+' && op != '-')
            return value;
        p->s++;
        int64_t rhs = parse_term(p);
        value = op == '+' ? value + rhs : value - rhs;
    }
}

static bool calculate(const char *expression, char *out, size_t out_size)
{
    parser_t parser = { expression, false };
    int64_t value = parse_expression(&parser);
    skip_space(&parser);
    if (parser.error || parser.s == expression || *parser.s != '\0')
        return false;
    ksnprintf(out, out_size, "%lld", value);
    return true;
}

static void calculator_clear(calculator_t *calc)
{
    calc->expression[0] = '\0';
    strlcpy(calc->display, "0", sizeof(calc->display));
    calc->just_evaluated = false;
}

static bool calculator_append(calculator_t *calc, char c)
{
    size_t length = strlen(calc->expression);
    if (length + 1 >= sizeof(calc->expression))
        return false;
    calc->expression[length] = c;
    calc->expression[length + 1] = '\0';
    strlcpy(calc->display, calc->expression, sizeof(calc->display));
    return true;
}

static void calculator_equals(calculator_t *calc)
{
    char result[64];
    if (!calc->expression[0])
        return;
    if (!calculate(calc->expression, result, sizeof(result))) {
        strlcpy(calc->display, "Error", sizeof(calc->display));
        calc->just_evaluated = true;
        return;
    }
    strlcpy(calc->expression, result, sizeof(calc->expression));
    strlcpy(calc->display, result, sizeof(calc->display));
    calc->just_evaluated = true;
}

static bool is_operator(char c)
{
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '%';
}

static bool calculator_key(window_t *win, const key_event_t *ev)
{
    calculator_t *calc = win->priv;
    if (ev->code == KEY_ESCAPE) {
        wm_close(win);
        return true;
    }
    if (ev->code == KEY_BACKSPACE) {
        size_t n = strlen(calc->expression);
        if (n)
            calc->expression[--n] = '\0';
        strlcpy(calc->display, n ? calc->expression : "0", sizeof(calc->display));
        calc->just_evaluated = false;
        wm_invalidate(win);
        return true;
    }
    char c = ev->ch;
    if (c == '=' || ev->code == KEY_ENTER) {
        calculator_equals(calc);
    } else if (c == 'c' || c == 'C') {
        calculator_clear(calc);
    } else if ((c >= '0' && c <= '9') || c == '(' || c == ')') {
        if (calc->just_evaluated) calculator_clear(calc);
        calculator_append(calc, c);
        calc->just_evaluated = false;
    } else if (is_operator(c)) {
        if (calc->just_evaluated) calc->just_evaluated = false;
        calculator_append(calc, c);
    } else {
        return false;
    }
    wm_invalidate(win);
    return true;
}

static const char *const calculator_buttons[] = {
    "C", "BS", "(", ")", "7", "8", "9", "/", "4", "5", "6", "*",
    "1", "2", "3", "-", "0", "%", "=", "+",
};

static void calculator_press(window_t *win, const char *label)
{
    key_event_t ev = { .ch = label[0] };
    if (!strcmp(label, "C")) ev.ch = 'c';
    else if (!strcmp(label, "BS")) ev.code = KEY_BACKSPACE;
    else if (!strcmp(label, "=")) ev.ch = '=';
    calculator_key(win, &ev);
}

static bool calculator_mouse(window_t *win, const mouse_event_t *ev, int lx, int ly)
{
    if (!(ev->pressed & MOUSE_LEFT))
        return true;
    if (ly < 58 || lx < 12 || lx >= 288)
        return true;
    int col = (lx - 12) / 66;
    int row = (ly - 58) / 42;
    if (col >= 0 && col < 4 && row >= 0 && row < 5)
        calculator_press(win, calculator_buttons[row * 4 + col]);
    return true;
}

static void calculator_draw(window_t *win, rect_t client)
{
    calculator_t *calc = win->priv;
    gfx_fill_rect(client.x, client.y, client.w, client.h, COLOR_WINDOW_BG);
    if (!calc) {
        widget_text_centered(client, "Out of memory", COLOR_DANGER);
        return;
    }
    rect_t display = rect_make(client.x + 12, client.y + 12, client.w - 24, 36);
    widget_panel(display, RGB(0x1c, 0x22, 0x2d), COLOR_BORDER);
    int text_x = display.x + display.w - gfx_text_width(calc->display) - 8;
    gfx_text(text_x, display.y + 10, calc->display,
             !strcmp(calc->display, "Error") ? COLOR_DANGER : COLOR_TERMINAL_TEXT);

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 4; col++) {
            int x = client.x + 12 + col * 66;
            int y = client.y + 58 + row * 42;
            const char *label = calculator_buttons[row * 4 + col];
            bool operator_button = is_operator(label[0]) || !strcmp(label, "=");
            widget_button(rect_make(x, y, 60, 34), label, false, false);
            if (operator_button)
                widget_text_centered(rect_make(x, y, 60, 34), label, COLOR_ACCENT_DARK);
        }
    }
}

static void calculator_open(window_t *win, const char *arg)
{
    calculator_t *calc = kcalloc(1, sizeof(*calc));
    win->priv = calc;
    if (calc) calculator_clear(calc);
}

static void calculator_close(window_t *win)
{
    kfree(win->priv);
    win->priv = NULL;
}

static const app_class_t calculator_class = {
    .name = "calculator", .on_open = calculator_open, .on_close = calculator_close,
    .on_draw = calculator_draw, .on_key = calculator_key, .on_mouse = calculator_mouse,
};

window_t *app_calculator_open(const char *arg)
{
    return wm_open(&calculator_class, 300, 300, "Calculator", arg);
}
