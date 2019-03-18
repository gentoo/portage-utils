/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2019-        Fabian Groffen <grobian@gentoo.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "main.h"
#include "xmalloc.h"

void *xmalloc(size_t size)
{
	void *ptr = malloc(size);
	if (unlikely(ptr == NULL))
		err("Out of memory");
	return ptr;
}

void *xcalloc(size_t nmemb, size_t size)
{
	void *ptr = calloc(nmemb, size);
	if (unlikely(ptr == NULL))
		err("Out of memory");
	return ptr;
}

void *xzalloc(size_t size)
{
	void *ptr = xmalloc(size);
	memset(ptr, 0x00, size);
	return ptr;
}

void *xrealloc(void *optr, size_t size)
{
	void *ptr = realloc(optr, size);
	if (unlikely(ptr == NULL))
		err("Out of memory");
	return ptr;
}

void *xmemdup(const void *src, size_t n)
{
	void *ret = xmalloc(n);
	memcpy(ret, src, n);
	return ret;
}

char *xstrdup_len(const char *s, size_t *len)
{
	if (s == NULL)
		return NULL;

	*len = strlen(s);
	return xmemdup(s, *len + 1);
}

char *xstrdup(const char *s)
{
	size_t len;

	return xstrdup_len(s, &len);
}
