/*
 * Copyright 2003-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2003-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2004-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _ARRAY_H
#define _ARRAY_H 1

#include <string.h>  /* strlen in push_str */

typedef struct array_t array;
typedef void (array_free_cb)(void *priv);

array *array_new(void);
void   array_free(array *arr);
void   array_deepfree(array *arr, array_free_cb *func);
void  *array_append(array *arr, void *data);
void  *array_append_copy(array *arr, const void *data, size_t len);
void  *array_remove(array *arr, size_t elem);
void   array_delete(array *arr, size_t elem, array_free_cb *func);
size_t array_cnt(array *arr);
void  *array_get(array *arr, size_t elem);
void   array_sort(array *arr, int (*compar)(const void *, const void *));
void  *array_binsearch(array *arr, void *needle, int (*compar)(const void *, const void *), size_t *retoff);

#define array_append_strcpy(A,S) array_append_copy(A,S,strlen(S)+1/*NUL*/)

#define array_for_each(arr, n, ele) \
	for (n = 0, ele = NULL; \
		 (n < array_cnt(arr) && \
		  (ele = array_get(arr, n))); \
		 n++)
#define array_for_each_rev(arr, n, ele) \
	for (n = array_cnt(arr), ele = NULL; \
		 (n-- > 0 && \
		  (ele = array_get(arr, n))); \
		 /*nothing*/)

#endif
