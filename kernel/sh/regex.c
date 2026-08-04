/* Backtracking regex matcher used by grep -E semantics: literals, '.', '*',
 * '+', '?', '^'/'$' anchors and '[...]' character classes. Small on purpose
 * (this is not POSIX ERE - no groups, no alternation, no back-references).
 */

#include <tinos/regex.h>

static char re_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool char_eq(char a, char b, bool ci)
{
    return ci ? re_lower(a) == re_lower(b) : a == b;
}

/* Length in `re` of the next atom (a literal, an escaped char, or a bracket
 * expression), not counting a trailing quantifier. */
static size_t atom_length(const char *re)
{
    if (re[0] == '\\' && re[1] != '\0')
        return 2;
    if (re[0] == '[') {
        const char *p = re + 1;
        if (*p == '^')
            p++;
        if (*p == ']')             /* a ']' right after '[' or '[^' is literal */
            p++;
        while (*p && *p != ']')
            p++;
        if (*p == ']')
            p++;
        return (size_t)(p - re);
    }
    return 1;
}

static bool class_matches(const char *re, size_t alen, char c, bool ci)
{
    bool        negate = re[1] == '^';
    const char *p      = re + (negate ? 2 : 1);
    const char *end    = re + alen - 1; /* points at the closing ']' */
    bool        hit    = false;

    while (p < end) {
        char lo = p[0];
        if (lo == '\\' && p + 1 < end) {
            lo = p[1];
            p += 2;
        } else if (p + 2 < end && p[1] == '-') {
            char hi = p[2];
            char cc = ci ? re_lower(c) : c;
            char rl = ci ? re_lower(lo) : lo;
            char rh = ci ? re_lower(hi) : hi;
            if (cc >= rl && cc <= rh)
                hit = true;
            p += 3;
            continue;
        } else {
            p += 1;
        }
        if (char_eq(lo, c, ci))
            hit = true;
    }
    return hit != negate;
}

static bool atom_matches(const char *re, size_t alen, char c, bool ci)
{
    if (re[0] == '\\')
        return char_eq(re[1], c, ci);
    if (re[0] == '.')
        return true;
    if (re[0] == '[')
        return class_matches(re, alen, c, ci);
    return char_eq(re[0], c, ci);
}

static bool match_here(const char *re, const char *text, bool ci);

static bool match_star(const char *re, size_t alen, const char *text, bool ci)
{
    const char *rest = re + alen + 1;
    const char *t     = text;
    for (;;) {
        if (match_here(rest, t, ci))
            return true;
        if (*t == '\0' || !atom_matches(re, alen, *t, ci))
            return false;
        t++;
    }
}

static bool match_here(const char *re, const char *text, bool ci)
{
    if (re[0] == '\0')
        return true;
    if (re[0] == '$' && re[1] == '\0')
        return *text == '\0';

    size_t alen  = atom_length(re);
    char   quant = re[alen];

    if (quant == '*')
        return match_star(re, alen, text, ci);

    if (quant == '+') {
        if (*text && atom_matches(re, alen, *text, ci))
            return match_star(re, alen, text + 1, ci);
        return false;
    }

    if (quant == '?') {
        if (*text && atom_matches(re, alen, *text, ci) &&
            match_here(re + alen + 1, text + 1, ci))
            return true;
        return match_here(re + alen + 1, text, ci);
    }

    if (*text && atom_matches(re, alen, *text, ci))
        return match_here(re + alen, text + 1, ci);
    return false;
}

bool regex_match(const char *pattern, const char *text, bool ignore_case)
{
    if (pattern[0] == '^')
        return match_here(pattern + 1, text, ignore_case);

    const char *t = text;
    do {
        if (match_here(pattern, t, ignore_case))
            return true;
    } while (*t++ != '\0');
    return false;
}
