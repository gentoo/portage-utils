/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <xalloc.h>

#include "set.h"
#include "array.h"

typedef struct setelem_ set_elem_t;
struct setelem_ {
  char         *name;
  unsigned int  hash;  /* FNV1a32 */
  void         *val;
  set_elem_t   *next;
};

#define _SET_HASH_SIZE 128
struct set_ {
  set_elem_t *buckets[_SET_HASH_SIZE];
  size_t      len;
};

static unsigned int
fnv1a32(const char *s)
{
  unsigned int ret = 2166136261UL;
  for (; *s != '\0'; s++)
    ret = (ret ^ (unsigned int)*s) * 16777619;
  return ret;
}

/* create a set */
set_t *set_new
(
  void
)
{
  return xzalloc(sizeof(set_t));
}

/* add elem to a set (unpure: could add duplicates, basically hash) */
set_t *set_add
(
  set_t      *s,
  const char *key
)
{
  set_elem_t *ll = xzalloc(sizeof(*ll));
  set_elem_t *w;
  int         pos;

  if (s == NULL)
    s = set_new();

  ll->name = xstrdup(key);
  ll->hash = fnv1a32(ll->name);

  pos = ll->hash % _SET_HASH_SIZE;
  if (s->buckets[pos] == NULL)
  {
    s->buckets[pos] = ll;
  }
  else
  {
    for (w = s->buckets[pos]; w->next != NULL; w = w->next)
      ;
    w->next = ll;
  }

  s->len++;
  return s;
}

/* add elem to set if it doesn't exist yet (pure definition of set) */
set_t *set_add_unique
(
  set_t      *q,
  const char *name,
  bool       *unique
)
{
  set_elem_t  *ll;
  set_elem_t  *w;
  unsigned int hash;
  int          pos;
  bool         uniq = false;

  if (q == NULL)
    q = set_new();

  hash = fnv1a32(name);
  pos  = hash % _SET_HASH_SIZE;

  if (q->buckets[pos] == NULL)
  {
    q->buckets[pos] = ll = xzalloc(sizeof(*ll));

    ll->name = xstrdup(name);
    ll->hash = hash;

    uniq = true;
  }
  else
  {
    ll = NULL;
    for (w = q->buckets[pos]; w != NULL; ll = w, w = w->next)
    {
      if (w->hash == hash &&
          strcmp(w->name, name) == 0)
      {
        uniq = false;
        break;
      }
    }
    if (w == NULL)
    {
      ll = ll->next = xzalloc(sizeof(*ll));

      ll->name = xstrdup(name);
      ll->hash = hash;

      uniq = true;
    }
  }

  if (uniq)
    q->len++;
  if (unique)
    *unique = uniq;
  return q;
}

/* splits buf on whitespace and adds the resulting tokens into the given
 * set, ignoring any duplicates
 * NOTE: if the input has keys > _Q_PATH_MAX this function misbehaves
 *       and doesn't function properly (currently there's no reasonable
 *       need for this though) */
set_t *set_add_from_string
(
  set_t      *s,
  const char *buf
)
{
  char        key[_Q_PATH_MAX];
  const char *p;
  size_t      len;

  if (buf == NULL)
    return s;

  if (s == NULL)
    s = set_new();

  for (p = buf, len = 0; len < sizeof(key) - 1; p++)
  {
    if (*p == '\0' ||
        isspace((int)*p))
    {
      if (len > 0)
      {
        key[len] = '\0';
        set_add_unique(s, key, NULL);
      }

      len = 0;
      if (*p == '\0')
        break;
      else
        continue;
    }

    key[len++] = *p;
  }

  return s;
}

/* returns whether name is in set, and if so, the set-internal key
 * representation (an internal copy of name made during addition) */
const char *set_get_key
(
  set_t      *q,
  const char *name
)
{
  set_elem_t   *w;
  const char   *found;
  unsigned int  hash;
  int           pos;

  if (q == NULL)
    return NULL;

  hash = fnv1a32(name);
  pos  = hash % _SET_HASH_SIZE;

  found = NULL;
  if (q->buckets[pos] != NULL)
  {
    for (w = q->buckets[pos]; w != NULL; w = w->next)
    {
      if (w->hash == hash &&
          strcmp(w->name, name) == 0)
      {
        found = w->name;
        break;
      }
    }
  }

  return found;
}

/* remove elem from a set. matches ->name and frees name,item, returns
 * val if removed, NULL otherwise
 * note that when val isn't set, NULL is returned, so the caller should
 * use the removed argument to determine if something was removed from
 * the set. */
void *set_delete
(
  set_t      *q,
  const char *s,
  bool       *removed
)
{
  set_elem_t  *ll;
  set_elem_t  *w;
  void        *ret;
  unsigned int hash;
  int          pos;
  bool         rmd;

  if (q == NULL)
  {
    if (removed != NULL)
      *removed = false;
    return NULL;
  }

  hash = fnv1a32(s);
  pos  = hash % _SET_HASH_SIZE;

  ret = NULL;
  rmd = false;
  if (q->buckets[pos] != NULL)
  {
    ll = NULL;
    for (w = q->buckets[pos]; w != NULL; ll = w, w = w->next)
    {
      if (w->hash == hash &&
          strcmp(w->name, s) == 0)
      {
        if (ll == NULL)
        {
          q->buckets[pos] = w->next;
        }
        else
        {
          ll->next = w->next;
        }
        ret = w->val;
        free(w->name);
        free(w);
        rmd = true;
        break;
      }
    }
  }

  if (rmd)
    q->len--;
  if (removed != NULL)
    *removed = rmd;
  return ret;
}

/* DEPRECATED -- use set_keys()/hash_keys() */
size_t array_set
(
  set_t *q,
  array *ret
)
{
  set_elem_t *w;
  int         i;

  /* allow using empty set */
  if (q == NULL)
    return 0;

  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = q->buckets[i]; w != NULL; w = w->next)
      array_append(ret, w->name);
  }

  return q->len;
}

/* DEPRECATED -- use hash_values() */
size_t values_set
(
  set_t *q,
  array *ret
)
{
  set_elem_t *w;
  int         i;

  /* allow using empty set */
  if (q == NULL)
    return 0;

  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = q->buckets[i]; w != NULL; w = w->next)
      array_append(ret, w->val);
  }

  return q->len;
}

size_t set_size
(
  set_t *q
)
{
  return q == NULL ? 0 : q->len;
}

/* clear out a set */
void clear_set
(
  set_t *q
)
{
  set_elem_t *w;
  set_elem_t *e;
  int         i;

  if (q == NULL)
    return;

  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = q->buckets[i]; w != NULL; w = e)
    {
      e = w->next;
      free(w->name);
      free(w);
    }
    q->buckets[i] = NULL;
  }
  q->len = 0;
}

/* clear and free a set */
void set_free(set_t *q)
{
  if (q == NULL)
    return;

  clear_set(q);
  free(q);
}

#ifdef EBUG
static void
set_print(const set_t *q)
{
  set_elem_t *w;
  int         i;

  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = q->buckets[i]; w != NULL; w = w->next)
      puts(w->name);
  }
}
#endif

hash_t *hash_new
(
  void
)
{
  return xzalloc(sizeof(hash_t));
}

/* add val to hash under key, return existing value when key
 * already exists or NULL otherwise */
hash_t *hash_add
(
  hash_t     *q,
  const char *key,
  void       *val,
  void      **prevval
)
{
  set_elem_t  *ll;
  set_elem_t  *w;
  unsigned int hash;
  int          pos;

  if (q == NULL)
    q = hash_new();

  hash = fnv1a32(key);
  pos  = hash % _SET_HASH_SIZE;

  if (prevval != NULL)
    *prevval = NULL;
  if (q->buckets[pos] == NULL)
  {
    q->buckets[pos] = ll = xzalloc(sizeof(*ll));
    ll->name = xstrdup(key);
    ll->hash = hash;
    ll->val  = val;
  }
  else
  {
    ll = NULL;
    for (w = q->buckets[pos]; w != NULL; ll = w, w = w->next)
    {
      if (w->hash == hash &&
          strcmp(w->name, key) == 0)
      {
        if (prevval != NULL)
          *prevval = w->val;
        return q;
      }
    }
    if (w == NULL)
    {
      ll = ll->next = xzalloc(sizeof(*ll));
      ll->name = xstrdup(key);
      ll->hash = hash;
      ll->val  = val;
    }
  }

  q->len++;
  return q;
}

/* returns the value for key, or NULL if not found (cannot
 * differentiate between value NULL and unset) */
void *hash_get
(
  hash_t     *q,
  const char *key
)
{
  set_elem_t  *w;
  unsigned int hash;
  int          pos;

  if (q == NULL)
    return NULL;

  hash = fnv1a32(key);
  pos  = hash % _SET_HASH_SIZE;

  if (q->buckets[pos] != NULL)
  {
    for (w = q->buckets[pos]; w != NULL; w = w->next)
    {
      if (w->hash == hash &&
          strcmp(w->name, key) == 0)
        return w->val;
    }
  }

  return NULL;
}

void *hash_delete_chk
(
  hash_t     *q,
  const char *key,
  bool       *removed
)
{
  return set_delete((set_t *)q, key, removed);
}

array *hash_keys
(
  hash_t *h
)
{
  array      *ret;
  set_elem_t *w;
  size_t      i;

  if (h == NULL)
    return NULL;

  ret = array_new();
  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = h->buckets[i]; w != NULL; w = w->next)
      array_append(ret, w->name);
  }

  return ret;
}

array *hash_values
(
  hash_t *h
)
{
  array      *ret;
  set_elem_t *w;
  size_t      i;

  if (h == NULL)
    return NULL;

  ret = array_new();
  for (i = 0; i < _SET_HASH_SIZE; i++)
  {
    for (w = h->buckets[i]; w != NULL; w = w->next)
      array_append(ret, w->val);
  }

  return ret;
}

size_t hash_size
(
  hash_t *h
)
{
  return h->len;
}

void hash_clear
(
  hash_t *h
)
{
  set_clear((set_t *)h);
}

void hash_free
(
  hash_t *h
)
{
  set_free((set_t *)h);
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
