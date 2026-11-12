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
static char result_label[48] = "Result: 0";
static int evaluated;

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
    tinos_strlcpy(result_label, "Result: ", sizeof(result_label));
    tinos_strlcpy(result_label + 8, result, sizeof(result_label) - 8);
    tinos_signal_set(&calculation, tinos_signal_get(&calculation) + 1);
}

static void clear(void)
{
    expression[0] = '\0';
    tinos_strlcpy(result, "0", sizeof(result));
    tinos_strlcpy(result_label, "Result: 0", sizeof(result_label));
    evaluated = 0;
    tinos_signal_set(&calculation, tinos_signal_get(&calculation) + 1);
}

static void on_action(tinos_widget_view_t *view, unsigned id, void *context)
{
    (void)view;
    (void)context;
    if (id == 1)
        calculate();
    else if (id == 2)
        clear();
}

int main(void)
{
    if (tinos_widget_open(&ui, "Calculator", 400, 250) < 0)
        return 1;
    tinos_signal_init(&calculation, 0);
    tinos_signal_watch(&calculation, &ui);
    ui.on_action = on_action;
    int banner = tinos_widget_add(&ui, 0, TINOS_WIDGET_BANNER, 0,
                                  "CALCULATOR / EXPRESSION", 0, 32, 0);
    int panel = tinos_widget_add(&ui, 0, TINOS_WIDGET_PANEL, 0, NULL,
                                 0, 50, 1);
    if (banner < 0 || panel < 0 ||
        tinos_widget_add(&ui, panel, TINOS_WIDGET_LABEL, 0,
                         result_label, 0, 24, 0) < 0 ||
        tinos_widget_field(&ui, 0, 1, expression, sizeof(expression), 0) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    int buttons = tinos_widget_add(&ui, 0, TINOS_WIDGET_ROW, 0, NULL,
                                   0, 46, 0);
    if (buttons < 0 ||
        tinos_widget_add(&ui, buttons, TINOS_WIDGET_BUTTON, 1,
                         "Evaluate", 104, 30, 1) < 0 ||
        tinos_widget_add(&ui, buttons, TINOS_WIDGET_BUTTON, 2,
                         "Clear", 88, 30, 1) < 0 ||
        tinos_widget_add(&ui, 0, TINOS_WIDGET_LABEL, 0,
                         "Type an expression. Enter to evaluate.", 0, 24, 0) < 0 ||
        tinos_widget_flush(&ui) < 0) {
        tinos_widget_close(&ui);
        return 1;
    }
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type == TINOS_GUI_KEY && event.ch == '=' &&
            ui.focused > 0 && ui.nodes[ui.focused].kind == TINOS_WIDGET_FIELD) {
            calculate();
        } else {
            if (evaluated && event.type == TINOS_GUI_KEY &&
                event.ch >= '0' && event.ch <= '9' &&
                ui.focused > 0 &&
                ui.nodes[ui.focused].kind == TINOS_WIDGET_FIELD)
                expression[0] = '\0';
            tinos_widget_event(&ui, &event);
            if (event.type == TINOS_GUI_KEY &&
                event.code != TINOS_KEY_ENTER &&
                event.code != TINOS_KEY_TAB)
                evaluated = 0;
        }
        if (tinos_widget_flush(&ui) < 0)
            break;
    }
    tinos_signal_unwatch(&calculation, &ui);
    tinos_widget_close(&ui);
    return 0;
}
