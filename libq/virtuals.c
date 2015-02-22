/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* used to queue a lot of things */
struct queue_t {
	char *name;
	char *item;
	struct queue_t *next;
};

typedef struct queue_t queue;

_q_static queue *
append_set(queue *q, queue *ll)
{
	queue *z;

	if (!q)
		return ll;

	z = q;
	while (z->next)
		z = z->next;
	z->next = ll;

	return q;
}

/* add a set to a cache */
_q_static queue *
add_set(const char *vv, const char *ss, queue *q)
{
	queue *ll;
	char *s, *ptr;
	char *v, *vptr;

	s = xstrdup(ss);
	v = xstrdup(vv);
	ptr = xmalloc(strlen(ss));
	vptr = xmalloc(strlen(vv));

	do {
		*ptr = 0;
		*vptr = 0;
		rmspace(ptr);
		rmspace(s);
		rmspace(vptr);
		rmspace(v);

		ll = xmalloc(sizeof(queue));
		ll->next = NULL;
		ll->name = xmalloc(strlen(v) + 1);
		ll->item = xmalloc(strlen(s) + 1);
		strcpy(ll->item, s);
		strcpy(ll->name, v);

		q = append_set(q, ll);

		*v = 0;
		strcpy(v, vptr);
		*s = 0;
		strcpy(s, ptr);

	} while (v[0]);

	free(s);
	free(ptr);
	free(v);
	free(vptr);

	return q;
}

/* remove a set from a cache. matches ->name and frees name,item */
_q_static queue *
del_set(char *s, queue *q, int *ok)
{
	queue *ll, *list, *old;
	ll = q;
	list = q;
	old = q;
	*ok = 0;

	while (ll != NULL) {
		if (strcmp(ll->name, s) == 0) {
			if (ll == list) {
				list = (ll->next);
				free(ll->name);
				free(ll->item);
				free(ll);
				ll = list;

			} else {
				old->next = ll->next;
				free(ll->name);
				free(ll->item);
				free(ll);
				ll = old->next;
			}
			*ok = 1;
		} else {
			old = ll;
			ll = ll->next;
		}
	}
	return list;
}

/* clear out a list */
_q_static void
free_sets(queue *list)
{
	queue *ll, *q;
	ll = list;
	while (ll != NULL) {
		q = ll->next;
		free(ll->name);
		free(ll->item);
		free(ll);
		ll = q;
	}
}

void print_sets(const queue *list);
void print_sets(const queue *list)
{
	const queue *ll;
	for (ll = list; ll != NULL; ll = ll->next)
		printf("%s -> %s\n", ll->name, ll->item);
}
