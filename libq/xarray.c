/*
 * Copyright 2003-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2003-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2004-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

typedef struct {
	void **eles;
	size_t num;
} array_t;

#define xrealloc_array(ptr, size, ele_size) xrealloc(ptr, (size) * (ele_size))
/* The assignment after the check is unfortunate as we do a non-NULL check (we
 * already do not permit pushing of NULL pointers), but we can't put it in the
 * increment phase as that will cause a load beyond the bounds of valid memory.
 */
#define array_for_each(arr, n, ele) \
	for (n = 0, ele = array_cnt(arr) ? arr->eles[n] : NULL; \
	     n < array_cnt(arr) && (ele = arr->eles[n]); \
	     ++n)
#define array_init_decl { .eles = NULL, .num = 0, }
#define array_cnt(arr) (arr)->num
#define DECLARE_ARRAY(arr) array_t _##arr = array_init_decl, *arr = &_##arr

static void *xarraypush(array_t *arr, const void *ele, size_t ele_len)
{
	void *nele;
	size_t n = arr->num++;
	arr->eles = xrealloc_array(arr->eles, arr->num, sizeof(ele));
	arr->eles[n] = nele = xmemdup(ele, ele_len);
	return nele;
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
