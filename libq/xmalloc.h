/*
 * Copyright 2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef XMALLOC_H
#define XMALLOC_H 1

#include <unistd.h>

void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xzalloc(size_t size);
void *xrealloc(void *optr, size_t size);
void *xmemdup(const void *src, size_t n);
char *xstrdup_len(const char *s, size_t *len);
char *xstrdup(const char *s);

#endif
