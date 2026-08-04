#pragma once

#include <tinos/types.h>

/* A small backtracking regex matcher for grep: literals, '.', '*', '+', '?',
 * '^' and '$' anchors, '[...]'/'[^...]' character classes with ranges, and
 * '\' to escape a metacharacter. Returns true if `pattern` matches anywhere
 * in `text` (or is anchored and matches at the required position). */
bool regex_match(const char *pattern, const char *text, bool ignore_case);
