/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _SET_H
#define _SET_H 1

#include "porting.h"
#include <stdlib.h>

typedef struct elem_t elem;
typedef struct set_t set;

struct elem_t {
	char *name;
	unsigned int hash;  /* FNV1a32 */
	elem *next;
};

#define _SET_HASH_SIZE 128
struct set_t {
	elem *buckets[_SET_HASH_SIZE];
	size_t len;
};

set *create_set(void);
set *add_set(const char *name, set *q);
set *add_set_unique(const char *name, set *q, bool *unique);
bool contains_set(char *s, set *q);
set *del_set(char *s, set *q, bool *removed);
size_t list_set(set *q, char ***l);
void free_set(set *q);

#endif
