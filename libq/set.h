/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _SET_H
#define _SET_H 1

#include <stdlib.h>
#include <unistd.h>

#include "array.h"

/* 2026 set API */
typedef struct set_ set_t;
set_t      *set_new(void);
set_t      *set_add(set_t *s, const char *key);
set_t      *set_add_unique(set_t *s, const char *key, bool *unique);
set_t      *set_add_from_string(set_t *s, const char *buf);
#define     set_contains(S,K)  (set_get(S,K) == NULL ? false : true)
const char *set_get(set_t *s, const char *key);
void       *set_delete(set_t *s, const char *key, bool *removed);
#define     set_keys(S)        hash_keys((hash_t *)S)
size_t      set_size(set_t *s);
void        set_clear(set_t *s);
void        set_free(set_t *s);

/* hash/dict interface */
typedef struct set_ hash_t;
hash_t *hash_new(void);
hash_t *hash_add(hash_t *h, const char *key, void *val, void **prevval);
void   *hash_get(hash_t *h, const char *key);
#define hash_delete(S,K)       hash_delete_chk(S,K,NULL)
void   *hash_delete_chk(hash_t *h, const char *key, bool *removed);
array  *hash_keys(hash_t *h);
array  *hash_values(hash_t *h);
size_t  hash_size(hash_t *h);
void    hash_clear(hash_t *h);
void    hash_free(hash_t *h);

/* backwards compat aliases and funcs */
typedef struct set_ set;
#define create_set(X)          set_new(X)
#define add_set(K,S)           set_add(S,K)
#define add_set_unique(K,S,U)  set_add_unique(S,K,U)
#define add_set_value(K,V,P,S) hash_add((hash_t *)S,K,V,P)
#define contains_set(K,S)      set_get(S,K)
#define get_set(K,S)           hash_get((hash_t *)S,K)
#define del_set(K,S,R)         set_delete(S,K,R)
#define cnt_set(S)             set_size(S)
#define clear_set(S)           set_clear(S)
#define free_set(S)            set_free(S)


#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
