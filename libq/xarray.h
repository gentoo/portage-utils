/*
 * Copyright 2003-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2003-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2004-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _XARRAY_H
#define _XARRAY_H 1

#include <stdlib.h>
#include <xalloc.h>

typedef struct {
	void **eles;
	size_t num;
	size_t len;
} array_t;

#define xrealloc_array(ptr, size, ele_size) xrealloc(ptr, (size) * (ele_size))
/* The assignment after the check is unfortunate as we do a non-NULL check (we
 * already do not permit pushing of NULL pointers), but we can't put it in the
 * increment phase as that will cause a load beyond the bounds of valid memory.
 */
/* TODO: remove ele = NULL after checking all consumers don't rely on this */
#define array_for_each(arr, n, ele) \
	for (n = 0, ele = NULL; n < array_cnt(arr) && (ele = arr->eles[n]); n++)
#define array_for_each_rev(arr, n, ele) \
	for (n = array_cnt(arr); n-- > 0 && (ele = arr->eles[n]); /*nothing*/)
#define array_get_elem(arr, n) (arr->eles[n])
#define array_init_decl { .eles = NULL, .num = 0, }
#define array_cnt(arr) (arr)->num
#define DECLARE_ARRAY(arr) array_t _##arr = array_init_decl, *arr = &_##arr
#define DEFINE_ARRAY(arr) array_t *arr;
#define xarraypush_str(arr, ele) xarraypush(arr, ele, strlen(ele) + 1 /*NUL*/)
#define xarraypush_struct(arr, ele) xarraypush(arr, ele, sizeof(*(ele)))

void *xarrayget(array_t *arr, size_t idx);
void *xarraypush_ptr(array_t *arr, void *ele);
void *xarraypush(array_t *arr, const void *ele, size_t ele_len);
void xarraydelete_ptr(array_t *arr, size_t elem);
void xarraydelete(array_t *arr, size_t elem);
void xarrayfree_int(array_t *arr);
void xarrayfree(array_t *arr);

#endif
