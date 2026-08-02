/* Command line parser.
 *
 * Grammar:
 *
 *   line     := segment (('&&' | '||' | ';' | '&') segment)*
 *   segment  := command ('|' command)*
 *   command  := word+ ('>' word | '>>' word | '<' word)*
 *
 * Words honour single and double quotes plus backslash escapes, and a
 * leading '~' expands to /home.  Each word is copied into an arena owned by
 * the parsed line (line->storage), so argv entries stay valid until
 * sh_line_free().
 */

#include <tinos/heap.h>
#include <tinos/printf.h>
#include <tinos/sh.h>
#include <tinos/string.h>

#define MAX_TOKENS 256
#define HOME_DIR   "/home"

typedef enum {
    TOK_WORD = 0,
    TOK_PIPE,
    TOK_REDIR_WRITE,
    TOK_REDIR_APPEND,
    TOK_REDIR_INPUT,
    TOK_SEQ,
    TOK_AND,
    TOK_OR,
} tok_type_t;

typedef struct {
    tok_type_t type;
    char      *text; /* words only */
} token_t;

typedef struct {
    char  *base;
    size_t used;
    size_t capacity;
} arena_t;

static void set_error(sh_line_t *line, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(line->error, sizeof(line->error), fmt, ap);
    va_end(ap);
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool is_operator(char c)
{
    return c == '|' || c == '&' || c == ';' || c == '>' || c == '<';
}

static void arena_putc(arena_t *a, char c)
{
    if (a->used + 1 < a->capacity)
        a->base[a->used++] = c;
}

static void arena_puts(arena_t *a, const char *s)
{
    while (*s)
        arena_putc(a, *s++);
}

/* Tokenise `input`, copying words into the arena. Returns -1 on error. */
static int tokenize(const char *input, arena_t *arena, token_t *tokens,
                    int max_tokens, sh_line_t *line)
{
    int         count = 0;
    const char *p     = input;

    while (*p) {
        while (is_space(*p))
            p++;
        if (!*p)
            break;
        if (count >= max_tokens) {
            set_error(line, "command line too long");
            return -1;
        }

        if (is_operator(*p)) {
            token_t *t = &tokens[count++];
            t->text    = NULL;
            if (p[0] == '|' && p[1] == '|') {
                t->type = TOK_OR;
                p += 2;
            } else if (p[0] == '&' && p[1] == '&') {
                t->type = TOK_AND;
                p += 2;
            } else if (p[0] == '>' && p[1] == '>') {
                t->type = TOK_REDIR_APPEND;
                p += 2;
            } else if (p[0] == '|') {
                t->type = TOK_PIPE;
                p++;
            } else if (p[0] == '>') {
                t->type = TOK_REDIR_WRITE;
                p++;
            } else if (p[0] == '<') {
                t->type = TOK_REDIR_INPUT;
                p++;
            } else {
                t->type = TOK_SEQ; /* '&' or ';' */
                p++;
            }
            continue;
        }

        /* A word. */
        char *start = arena->base + arena->used;

        /* '~' or '~/...' at the start of a word means the home directory. */
        if (p[0] == '~' && (p[1] == '\0' || p[1] == '/' || is_space(p[1]))) {
            arena_puts(arena, HOME_DIR);
            p++;
        }

        while (*p && !is_space(*p) && !is_operator(*p)) {
            if (*p == '\'' || *p == '"') {
                char quote = *p++;
                while (*p && *p != quote) {
                    if (quote == '"' && *p == '\\' && p[1])
                        p++;
                    arena_putc(arena, *p++);
                }
                if (*p != quote) {
                    set_error(line, "unterminated quote");
                    return -1;
                }
                p++;
                continue;
            }
            if (*p == '\\' && p[1]) {
                p++;
                arena_putc(arena, *p++);
                continue;
            }
            arena_putc(arena, *p++);
        }
        arena_putc(arena, '\0');

        tokens[count].type = TOK_WORD;
        tokens[count].text = start;
        count++;
    }
    return count;
}

bool sh_parse(const char *input, sh_line_t *line)
{
    memset(line, 0, sizeof(*line));
    if (!input)
        input = "";

    /* The arena never needs more than the input plus one terminator per word
     * plus the home directory expansion, so twice the input is ample. */
    size_t input_len = strlen(input);
    size_t capacity  = input_len * 2 + sizeof(HOME_DIR) + 16;
    line->storage    = kmalloc(capacity);
    if (!line->storage) {
        set_error(line, "out of memory");
        return false;
    }
    arena_t arena = { line->storage, 0, capacity };

    token_t tokens[MAX_TOKENS];
    int     ntokens = tokenize(input, &arena, tokens, MAX_TOKENS, line);
    if (ntokens < 0)
        return false;
    if (ntokens == 0)
        return true; /* blank line: nothing to do, not an error */

    sh_segment_t *segment = &line->segments[0];
    line->nsegments       = 1;
    segment->join         = SH_JOIN_FIRST;
    segment->nstages      = 1;
    sh_command_t *cmd     = &segment->stages[0];

    for (int i = 0; i < ntokens; i++) {
        token_t *t = &tokens[i];

        switch (t->type) {
        case TOK_WORD:
            if (cmd->argc >= SH_MAX_ARGS) {
                set_error(line, "too many arguments (limit %d)", SH_MAX_ARGS);
                return false;
            }
            cmd->argv[cmd->argc++] = t->text;
            cmd->argv[cmd->argc]   = NULL;
            break;

        case TOK_REDIR_WRITE:
        case TOK_REDIR_APPEND:
        case TOK_REDIR_INPUT: {
            const char *op = t->type == TOK_REDIR_INPUT     ? "<"
                             : t->type == TOK_REDIR_APPEND  ? ">>"
                                                            : ">";
            if (i + 1 >= ntokens || tokens[i + 1].type != TOK_WORD) {
                set_error(line, "missing file name after '%s'", op);
                return false;
            }
            char *path = tokens[++i].text;
            if (t->type == TOK_REDIR_INPUT) {
                cmd->input_path = path;
            } else {
                cmd->redirect      = t->type == TOK_REDIR_APPEND
                                         ? SH_REDIR_APPEND
                                         : SH_REDIR_WRITE;
                cmd->redirect_path = path;
            }
            break;
        }

        case TOK_PIPE:
            if (cmd->argc == 0) {
                set_error(line, "syntax error near '|'");
                return false;
            }
            if (i + 1 >= ntokens) {
                set_error(line, "syntax error: '|' at end of line");
                return false;
            }
            if (segment->nstages >= SH_MAX_STAGES) {
                set_error(line, "pipeline too long (limit %d stages)",
                          SH_MAX_STAGES);
                return false;
            }
            cmd = &segment->stages[segment->nstages++];
            break;

        case TOK_SEQ:
        case TOK_AND:
        case TOK_OR:
            if (cmd->argc == 0) {
                set_error(line, "syntax error: operator without a command");
                return false;
            }
            if (i + 1 >= ntokens)
                return true; /* a trailing separator is harmless */
            if (line->nsegments >= SH_MAX_SEGMENTS) {
                set_error(line, "too many commands (limit %d)",
                          SH_MAX_SEGMENTS);
                return false;
            }
            segment          = &line->segments[line->nsegments++];
            segment->join    = t->type == TOK_AND  ? SH_JOIN_AND
                               : t->type == TOK_OR ? SH_JOIN_OR
                                                   : SH_JOIN_SEQ;
            segment->nstages = 1;
            cmd              = &segment->stages[0];
            break;
        }
    }

    /* A pipeline stage with no command (e.g. "ls | > f") is a syntax error. */
    for (int s = 0; s < line->nsegments; s++)
        for (int k = 0; k < line->segments[s].nstages; k++)
            if (line->segments[s].stages[k].argc == 0) {
                set_error(line, "syntax error: empty command");
                return false;
            }

    return true;
}

void sh_line_free(sh_line_t *line)
{
    kfree(line->storage);
    line->storage = NULL;
}
