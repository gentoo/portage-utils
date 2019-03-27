/*
 * Copyright 2011-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _XREGEX_H
#define _XREGEX_H 1

#include <regex.h>

int wregcomp(regex_t *preg, const char *regex, int cflags);
void xregcomp(regex_t *preg, const char *regex, int cflags);
int rematch(const char *, const char *, int);

#endif
