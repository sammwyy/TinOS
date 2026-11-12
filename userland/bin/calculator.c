#include <tinos/string.h>
#include <tinos/ui.h>

typedef struct {
    const char *input;
    unsigned depth;
    int invalid;
} parser_t;

static tinos_ui_t ui;
static char expression[64];
static char result[32] = "0";
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
}

static void render(void)
{
    tinos_ui_clear(&ui);
    tinos_ui_text(&ui, 2, 1, "Calculator");
    tinos_ui_text(&ui, 2, 3, "Expression");
    tinos_ui_text(&ui, 2, 4, expression);
    tinos_ui_text(&ui, 2, 6, "Result");
    tinos_ui_text(&ui, 2, 7, result);
    tinos_ui_text(&ui, 2, 10, "Enter or = evaluate    C clear");
    tinos_ui_text(&ui, 2, 11, "+  -  *  /  %  (  )");
    tinos_ui_present(&ui);
}

int main(void)
{
    if (tinos_ui_open(&ui, "Calculator", 400, 250) < 0)
        return 1;
    render();
    tinos_gui_event_t event;
    while (tinos_gui_event(ui.handle, &event) == 1) {
        if (event.type == TINOS_GUI_CLOSE ||
            (event.type == TINOS_GUI_KEY &&
             event.code == TINOS_KEY_ESCAPE))
            break;
        if (event.type != TINOS_GUI_KEY)
            continue;
        if (event.code == TINOS_KEY_ENTER || event.ch == '=') {
            calculate();
        } else if (event.ch == 'c' || event.ch == 'C') {
            expression[0] = '\0';
            tinos_strlcpy(result, "0", sizeof(result));
            evaluated = 0;
        } else if (event.code == TINOS_KEY_BACKSPACE) {
            size_t length = strlen(expression);
            if (length)
                expression[length - 1] = '\0';
            evaluated = 0;
        } else if ((event.ch >= '0' && event.ch <= '9') ||
                   event.ch == '+' || event.ch == '-' ||
                   event.ch == '*' || event.ch == '/' ||
                   event.ch == '%' || event.ch == '(' ||
                   event.ch == ')') {
            if (evaluated && event.ch >= '0' && event.ch <= '9')
                expression[0] = '\0';
            size_t length = strlen(expression);
            if (length + 1 < sizeof(expression)) {
                expression[length] = event.ch;
                expression[length + 1] = '\0';
            }
            evaluated = 0;
        }
        render();
    }
    tinos_gui_close(ui.handle);
    return 0;
}
