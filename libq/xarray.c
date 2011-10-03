/*
 * Copyright 2003-2007 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xarray.c,v 1.1 2011/10/03 16:18:25 vapier Exp $
 *
 * Copyright 2003-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2004-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

typedef struct {
	void **eles;
	size_t num;
} array_t;

#define xrealloc_array(ptr, size, ele_size) xrealloc(ptr, (size) * (ele_size))
#define array_for_each(arr, n, ele) \
	for (n = 0, ele = arr->eles[n]; n < arr->num; ++n, ele = arr->eles[n])
#define array_init_decl { .eles = NULL, .num = 0, }
#define array_cnt(arr) (arr)->num
#define DECLARE_ARRAY(arr) array_t _##arr = array_init_decl, *arr = &_##arr

static void xarraypush(array_t *arr, const void *ele, size_t ele_len)
{
	size_t n = arr->num++;
	arr->eles = xrealloc_array(arr->eles, arr->num, sizeof(ele));
	arr->eles[n] = xmemdup(ele, ele_len);
}
#define xarraypush_str(arr, ele) xarraypush(arr, ele, strlen(ele) + 1 /*NUL*/)

static void xarrayfree(array_t *arr)
{
	array_t blank = array_init_decl;
	size_t n;

	for (n = 0; n < arr->num; ++n)
		free(arr->eles[n]);
	free(arr->eles);

	*arr = blank;
}
