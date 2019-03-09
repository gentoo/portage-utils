/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

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

static unsigned int
fnv1a32(char *s)
{
	unsigned int ret = 2166136261UL;
	for (; *s != '\0'; s++)
		ret = (ret ^ (unsigned int)*s) * 16777619;
	return ret;
}

/* create a set */
static set *
create_set(void)
{
	return xzalloc(sizeof(set));
}

/* add elem to a set (unpure: could add duplicates) */
static set *
add_set(const char *name, set *q)
{
	int pos;
	elem *ll = xmalloc(sizeof(*ll));
	elem *w;

	if (q == NULL)
		q = create_set();

	ll->next = NULL;
	ll->name = xstrdup(name);
	rmspace(ll->name);
	ll->hash = fnv1a32(ll->name);

	pos = ll->hash % _SET_HASH_SIZE;
	if (q->buckets[pos] == NULL) {
		q->buckets[pos] = ll;
	} else {
		for (w = q->buckets[pos]; w->next != NULL; w = w->next)
			;
		w->next = ll;
	}

	q->len++;
	return q;
}

/* add elem to set if it doesn't exist yet (pure definition of set) */
static set *
add_set_unique(const char *name, set *q, bool *unique)
{
	char *mname = xstrdup(name);
	int hash;
	int pos;
	elem *ll;
	elem *w;

	if (q == NULL)
		q = create_set();

	rmspace(mname);
	hash = fnv1a32(mname);
	pos = hash % _SET_HASH_SIZE;

	if (q->buckets[pos] == NULL) {
		q->buckets[pos] = ll = xmalloc(sizeof(*ll));
		ll->next = NULL;
		ll->name = mname;
		ll->hash = hash;
		*unique = true;
	} else {
		ll = NULL;
		for (w = q->buckets[pos]; w != NULL; ll = w, w = w->next) {
			if (w->hash == hash && strcmp(w->name, mname) == 0) {
				free(mname);
				*unique = false;
				break;
			}
		}
		if (w == NULL) {
			ll = ll->next = xmalloc(sizeof(*ll));
			ll->next = NULL;
			ll->name = mname;
			ll->hash = hash;
			*unique = true;
		}
	}

	if (*unique)
		q->len++;
	return q;
}

/* returns whether s is in set */
static bool
contains_set(char *s, set *q)
{
	char *mname = xstrdup(s);
	int hash;
	int pos;
	elem *w;
	bool found;

	rmspace(mname);
	hash = fnv1a32(mname);
	pos = hash % _SET_HASH_SIZE;

	found = false;
	if (q->buckets[pos] != NULL) {
		for (w = q->buckets[pos]; w != NULL; w = w->next) {
			if (w->hash == hash && strcmp(w->name, mname) == 0) {
				found = true;
				break;
			}
		}
	}

	free(mname);
	return found;
}

/* remove elem from a set. matches ->name and frees name,item */
static set *
del_set(char *s, set *q, bool *removed)
{
	char *mname = xstrdup(s);
	unsigned int hash;
	int pos;
	elem *ll;
	elem *w;

	rmspace(mname);
	hash = fnv1a32(mname);
	pos = hash % _SET_HASH_SIZE;

	*removed = false;
	if (q->buckets[pos] != NULL) {
		ll = NULL;
		for (w = q->buckets[pos]; w != NULL; ll = w, w = w->next) {
			if (w->hash == hash && strcmp(w->name, mname) == 0) {
				free(mname);
				if (ll == NULL) {
					q->buckets[pos] = w->next;
				} else {
					ll->next = w->next;
				}
				free(w->name);
				free(w);
				*removed = true;
				break;
			}
		}
	}

	if (*removed)
		q->len--;
	return q;
}

/* return the contents of a set as an array of strings
 * the length of the list is returned, and the array is terminated with
 * a NULL (not included in returned length)
 * the caller should free l, but not the strings within */
static size_t
list_set(set *q, char ***l)
{
	int i;
	elem *w;
	char **ret;

	ret = *l = xmalloc(sizeof(char **) * (q->len + 1));
	for (i = 0; i < _SET_HASH_SIZE; i++) {
		for (w = q->buckets[i]; w != NULL; w = w->next) {
			*ret = w->name;
			ret++;
		}
	}
	*ret = NULL;
	return q->len;
}

/* clear out a set */
static void
free_set(set *q)
{
	int i;
	elem *w;
	elem *e;

	for (i = 0; i < _SET_HASH_SIZE; i++) {
		for (w = q->buckets[i]; w != NULL; w = e) {
			e = w->next;
			free(w->name);
			free(w);
		}
	}
	free(q);
}

#ifdef EBUG
static void
print_set(const set *q)
{
	elem *w;
	int i;

	for (i = 0; i < _SET_HASH_SIZE; i++) {
		for (w = q->buckets[i]; w != NULL; w = w->next)
			puts(w->name);
	}
}
#endif
