#pragma once

/* Detect dominant layout: returns 0 for EN, 1 for RU */
int   layout_detect(const char *utf8);

/* Convert text to opposite layout (auto-detects direction).
 * Returns heap-allocated string; caller must free(). */
char *layout_convert(const char *utf8);
