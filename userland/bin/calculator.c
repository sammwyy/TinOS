#include <tinos/string.h>
#include <tinos/widgets.h>

typedef struct {
    const char *input;
    unsigned depth;
    int invalid;
} parser_t;

static tinos_widget_view_t ui;
static tinos_signal_t calculation;
static char expression[64];
static char result[32] = "0";
static int evaluated;
static const char *const buttons[20] = {
    "C", "BS", "(", ")", "7", "8", "9", "/", "4", "5", "6", "*",
    "1", "2", "3", "-", "0", "%", "=", "+",
};

static int32_t parse_expression(parser_t *parser);

static int32_t parse_factor(parser_t *parser)
{
    if (++parser->depth > 16) {
        parser->invalid = 1;
        return 0;
    }
    int negative = *parser->input == '-';
    if (negative || *parser->input == '+')
        parser->input++;
    int32_t value = 0;
    if (*parser->input == '(') {
        parser->input++;
        value = parse_expression(parser);
        if (*parser->input++ != ')')
            parser->invalid = 1;
    } else if (*parser->input >= '0' && *parser->input <= '9') {
        do {
            int digit = *parser->input++ - '0';
            if (value > (INT32_MAX - digit) / 10)
                parser->invalid = 1;
            else
                value = value * 10 + digit;
        } while (*parser->input >= '0' && *parser->input <= '9');
    } else {
        parser->invalid = 1;
    }
    parser->depth--;
    return negative ? -value : value;
}

static int32_t parse_term(parser_t *parser)
{
    int32_t value = parse_factor(parser);
    while (!parser->invalid) {
        char op = *parser->input;
        if (op != '*' && op != '/' && op != '%')
            break;
        parser->input++;
        int32_t rhs = parse_factor(parser);
        if (op == '*') {
            if (__builtin_mul_overflow(value, rhs, &value))
                parser->invalid = 1;
        } else if (!rhs || (value == INT32_MIN && rhs == -1)) {
            parser->invalid = 1;
        } else {
            value = op == '/' ? value / rhs : value % rhs;
        }
    }
    return value;
}

static int32_t parse_expression(parser_t *parser)
{
    int32_t value = parse_term(parser);
    while (!parser->invalid) {
        char op = *parser->input;
        if (op != '+' && op != '-')
            break;
        parser->input++;
        int32_t rhs = parse_term(parser);
        if (op == '+') {
            if (__builtin_add_overflow(value, rhs, &value))
                parser->invalid = 1;
        } else if (__builtin_sub_overflow(value, rhs, &value)) {
            parser->invalid = 1;
        }
    }
    return value;
}

static void calculate(void)
{
    parser_t parser = { .input = expression };
    int32_t value = parse_expression(&parser);
    if (parser.invalid || !expression[0] || *parser.input) {
        tinos_strlcpy(result, "Error", sizeof(result));
    } else {
        if (value < 0) {
            result[0] = '-';
            tinos_u32(result + 1, (uint32_t)(-(int64_t)value));
        } else {
            tinos_u32(result, (uint32_t)value);
        }
    }
    evaluated = 1;
    tinos_signal_set(&calculation, tinos_signal_get(&calculation) + 1);
}

static void clear(void)
{
    expression[0] = '\0';
    tinos_strlcpy(result, "0", sizeof(result));
    evaluated = 0;
    tinos_signal_set(&calculation, tinos_signal_get(&calculation) + 1);
}

static void grid_metrics(int *width, int *height, int *gap)
{
    *gap = 6;
    *width = ((int)ui.width - 24 - 3 * *gap) / 4;
    *height = ((int)ui.height - 58 - 12 - 4 * 8) / 5;
    if (*height < 24)
        *height = 24;
}

static void paint(tinos_widget_view_t *view, tinos_canvas_t *canvas,
                  void *context)
{
    (void)context;
    const tinos_gui_theme_t *theme = &view->theme;
    int w, h, gap;
    grid_metrics(&w, &h, &gap);
    tinos_canvas_fill(canvas, 0, 0, view->width, view->height,
                      theme->background);
    tinos_canvas_fill(canvas, 12, 12, view->width - 24, 36, 0x1c222du);
    tinos_canvas_frame(canvas, 12, 12, view->width - 24, 36,
                       theme->border);
    int length = strlen(evaluated ? result : expression);
    const char *shown = evaluated ? result :
                        (length ? expression : result);
    tinos_canvas_text(canvas, (int)view->width - 20 - (int)strlen(shown) * 8,
                      22, view->width - 32, shown,
                      result[0] == 'E' ? theme->danger : theme->terminal_text);
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 4; col++) {
            const char *label = buttons[row * 4 + col];
            int x = 12 + col * (w + gap);
            int y = 58 + row * (h + 8);
            tinos_canvas_button(canvas, x, y, w, h, NULL, false);
            int len = strlen(label);
            bool special = label[0] == '+' || label[0] == '-' ||
                           label[0] == '*' || label[0] == '/' ||
                           label[0] == '%' || label[0] == '=';
            tinos_canvas_text(canvas, x + (w - len * 8) / 2,
                              y + (h - 16) / 2, w, label,
                              special ? theme->accent : theme->foreground);
        }
}

static void press(const char *label)
{
    if (label[0] == '=')
        calculate();
    else if (label[0] == 'C')
        clear();
    else if (label[0] == 'B') {
        size_t len = strlen(expression);
        if (len)
            expression[len - 1] = '\0';
        evaluated = 0;
    } else {
        if (evaluated && label[0] >= '0' && label[0] <= '9')
            expression[0] = '\0';
        size_t len = strlen(expression);
        if (len + 1 < sizeof(expression)) {
            expression[len] = label[0];
            expression[len + 1] = '\0';
        }
        evaluated = 0;
    }
    ui.dirty = true;
}

int main(void)
{
    if (tinos_widget_open(&ui, "Calculator", 300, 300) < 0)
        return 1;
    tinos_signal_init(&calculation, 0);
    tinos_signal_watch(&calculation, &ui);
    ui.on_paint = paint;
    if (tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_KEY) {
            char label[2] = { (char)event.ch, '\0' };
            if (event.code == TINOS_KEY_ENTER)
                press("=");
            else if (event.code == TINOS_KEY_BACKSPACE)
                press("BS");
            else if ((event.ch >= '0' && event.ch <= '9') ||
                     event.ch == '+' || event.ch == '-' || event.ch == '*' ||
                     event.ch == '/' || event.ch == '%' || event.ch == '(' ||
                     event.ch == ')' || event.ch == '=' ||
                     event.ch == 'c' || event.ch == 'C')
                press(event.ch == 'c' ? "C" : label);
        } else if (event.type == TINOS_GUI_MOUSE && event.code == 1) {
            int w, h, gap;
            grid_metrics(&w, &h, &gap);
            for (int row = 0; row < 5; row++)
                for (int col = 0; col < 4; col++) {
                    int x = 12 + col * (w + gap), y = 58 + row * (h + 8);
                    if (event.x >= x && event.y >= y &&
                        event.x - x < w && event.y - y < h)
                        press(buttons[row * 4 + col]);
                }
        } else if (event.type == TINOS_GUI_RESIZE) {
            ui.dirty = true;
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_signal_unwatch(&calculation, &ui);
    tinos_widget_close(&ui);
    return 0;
}
