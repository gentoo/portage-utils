/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _SET_H
#define _SET_H 1

#include <stdlib.h>
#include <unistd.h>

#include "array.h"

typedef struct setelem_t set_elem;
typedef struct set_t set;

struct setelem_t {
	char *name;
	unsigned int hash;  /* FNV1a32 */
	void *val;
	set_elem *next;
};

#define _SET_HASH_SIZE 128
struct set_t {
	set_elem *buckets[_SET_HASH_SIZE];
	size_t len;
};

set *create_set(void);
set *add_set(const char *name, set *q);
set *add_set_unique(const char *name, set *q, bool *unique);
set *add_set_value(const char *name, void *ptr, void **prevptr, set *q);
const char *contains_set(const char *name, set *q);
void *get_set(const char *name, set *q);
void *del_set(const char *s, set *q, bool *removed);
size_t list_set(set *q, char ***l);
size_t array_set(set *q, array *ret);
size_t values_set(set *q, array *ret);
size_t cnt_set(set *q);
void free_set(set *q);
void clear_set(set *q);

/* 2026 forward API */
typedef struct set_t set_t;
#define set_new()              create_set()
#define set_add(S,K)           add_set(K,S)
#define set_add_unique(S,K,U)  add_set_unique(K,S,U)
#define set_contains(S,K)      (contains_set(K,S) == NULL ? false : true)
#define set_get_key(S,K)       contains_set(K,S)
#define set_delete(S,K)        del_set(K,S,NULL)
#if 0
#define set_keys(S)            TODO
#endif
#define set_size(S)            cnt_set(S)
#define set_clear(S)           clear_set(S)
#define set_free(S)            free_set(S)

typedef struct set_t hash_t;
#define hash_new()             create_set()
#define hash_add(S,K,V,P)      add_set_value(K,V,P,S)
#define hash_get(S,K)          get_set(K,S)
#define hash_delete(S,K)       del_set(K,S,NULL)
#define hash_delete_chk(S,K,R) del_set(K,S,R)
#if 0
#define hash_keys(S)           TODO
#define hash_values(S)         TODO
#endif
#define hash_size(S)           cnt_set(S)
#define hash_clear(S)          clear_set(S)
#define hash_free(S)           free_set(S)


#endif
