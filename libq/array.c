/*
 * Copyright 2003-2026 Gentoo Foundation
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

#include "array.h"

#define ARRAY_INC_SIZE 32

struct array_t {
	void **eles;
	size_t len;
	size_t siz;
};

/* allocates a new array */
array *array_new(void)
{
	return xzalloc(sizeof(array));
}

/* frees an array without freeing any of the data stored in the array */
void array_free(array *arr)
{
	if (arr == NULL)
		return;

	free(arr->eles);
	free(arr);
}

/* frees the array and all the data pointed to by all of its elements
 * if the callback free function is NULL, free() is used */
void array_deepfree(array *arr, array_free_cb *func)
{
	size_t i;

	if (arr == NULL)
		return;

	if (func == NULL)
		func = &free;

	for (i = 0; i < arr->len; i++)
		func(arr->eles[i]);

	array_free(arr);
}

/* appends the given pointer to the list, no copying of data takes place
 * returns the pointer */
void *array_append(array *arr, void *data)
{
	size_t n;

	if (arr == NULL)
		return NULL;

	n = arr->len++;

	if (arr->len > arr->siz)
	{
		arr->siz += ARRAY_INC_SIZE;
		arr->eles = xrealloc(arr->eles, arr->siz * sizeof(arr->eles[0]));
	}

	return arr->eles[n] = data;
}

/* copies data of len bytes and appends to the array
 * returns the pointer to the copied data */
void *array_append_copy(array *arr, const void *data, size_t len)
{
	void *ret = NULL;

	if (data != NULL &&
		len > 0)
	{
		ret = xmemdup(data, len);
	}

	if (array_append(arr, ret) == NULL &&
		ret != NULL)
	{
		free(ret);
		ret = NULL;
	}

	return ret;
}

/* removes the given element from the array and returns the pointer to
 * the data removed from the array
 * the caller should ensure the pointer is freed if necessary */
void *array_remove(array *arr, size_t elem)
{
	void *ret;

	if (arr == NULL ||
		elem >= arr->len)
	{
		return NULL;
	}

	ret = arr->eles[elem];
	arr->len--;
	if (elem < arr->len)
	{
		memmove(&arr->eles[elem], &arr->eles[elem + 1],
				sizeof(arr->eles[0]) * (arr->len - elem));
	}

	return ret;
}

/* frees the element at offset elem and removes it from the list, if the
 * callback free function is NULL, free() is used */
void array_delete(array *arr, size_t elem, array_free_cb *func)
{
	if (arr == NULL)
		return;

	if (func == NULL)
		func = &free;

	func(arr->eles[elem]);

	(void)array_remove(arr, elem);
}

/* returns the number of elements in use */
size_t array_cnt(array *arr)
{
	if (arr == NULL)
		return 0;

	return arr->len;
}

/* returns the element at the given offset, or NULL when no such element
 * exists
 * note that NULL can also be returned if the element has the value of
 * NULL, so if the caller wants to know the difference, it should check
 * the input is sane using array_cnt() */
void *array_get(array *arr, size_t elem)
{
	if (arr == NULL)
		return NULL;

	if (arr->len <= elem)
		return NULL;

	return arr->eles[elem];
}

/* sorts the elements in the array using the given comparator */
void array_sort(array *arr, int (*compar)(const void *, const void *))
{
	if (arr != NULL &&
		arr->len > 1)
	{
		qsort(arr->eles, arr->len, sizeof(void *), compar);
	}
}

/* binary search over the array, returning the first element for which
 * compar(needle, elem(N)) returns 0
 * note that this function assumes the array is sorted in a way
 * compatible with compar, the caller should ensure this
 * returns the found element, if any, and its offset in retoff when set
 * note that if the found element is NULL, the caller should retrieve
 * and compare the element at retoff to check if the value was found */
void *array_binsearch
(
	array  *arr,
	void   *needle,
	int   (*compar)(const void *, const void *),
	size_t *retoff
)
{
	size_t low;
	size_t high;
	size_t elem;
	int    cmp;

	if (arr == NULL ||
		compar == NULL)
	{
		return NULL;
	}

	low  = 0;
	high = arr->len;

	while (low != high)
	{
		elem = low + ((high - low) / 2);
		cmp  = compar(needle, arr->eles[elem]);
		if (cmp == 0)
		{
			if (retoff != NULL)
				*retoff = elem;
			return arr->eles[elem];
		}
		else if (cmp < 0)
		{
			high = elem;
		}
		else if (cmp > 0)
		{
			low = elem + 1;
		}
	}

	*retoff = 0;
	return NULL;
}
