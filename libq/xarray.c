/*
 * Copyright 2003-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2003-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2004-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <xalloc.h>

#include "xarray.h"

#define ARRAY_INC_SIZE 32

void *xarrayget(array_t *arr, size_t idx)
{
	if (idx >= arr->num)
		return NULL;
	return arr->eles[idx];
}

/* Push a pointer to memory we already hold and don't want to release.  Do not
 * mix xarraypush_ptr usage with the other push funcs which duplicate memory.
 * The free stage won't know which pointers to release directly.
 */
void *xarraypush_ptr(array_t *arr, void *ele)
{
	size_t n = arr->num++;
	if (arr->num > arr->len) {
		arr->len += ARRAY_INC_SIZE;
		arr->eles = xrealloc_array(arr->eles, arr->len, sizeof(ele));
	}
	arr->eles[n] = ele;
	return ele;
}

void *xarraypush(array_t *arr, const void *ele, size_t ele_len)
{
	return xarraypush_ptr(arr, xmemdup(ele, ele_len));
}

void xarraysort(array_t *arr, int (*compar)(const void *, const void *))
{
	qsort(arr->eles, arr->num, sizeof(void *), compar);
}

void xarraydelete_ptr(array_t *arr, size_t elem)
{
	arr->num--;
	if (elem < arr->num)
		memmove(&arr->eles[elem], &arr->eles[elem + 1],
				sizeof(arr->eles[0]) * (arr->num - elem));
	arr->eles[arr->num] = NULL;
}

void xarraydelete(array_t *arr, size_t elem)
{
	free(arr->eles[elem]);
	xarraydelete_ptr(arr, elem);
}

/* Useful for people who call xarraypush_ptr as it does not free any of the
 * pointers in the eles list.
 */
void xarrayfree_int(array_t *arr)
{
	array_t blank = array_init_decl;
	free(arr->eles);
	*arr = blank;
}

void xarrayfree(array_t *arr)
{
	size_t n;
	for (n = 0; n < arr->num; ++n)
		free(arr->eles[n]);
	xarrayfree_int(arr);
}
