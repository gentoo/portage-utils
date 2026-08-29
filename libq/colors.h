/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _COLORS_H
#define _COLORS_H 1

#include <stdbool.h>

extern const char *BOLD;
extern const char *NORM;
extern const char *BLUE;
extern const char *DKBLUE;
extern const char *CYAN;
extern const char *GREEN;
extern const char *DKGREEN;
extern const char *MAGENTA;
extern const char *RED;
extern const char *YELLOW;
extern const char *BRYELLOW;
extern const char *WHITE;

void color_remap(void);
void color_clear(void);
bool color_mapped(void);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
