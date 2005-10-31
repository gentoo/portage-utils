/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/virtuals.c,v 1.7 2005/10/31 14:37:01 solar Exp $
 *
 * Copyright 2005 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/virtuals.c,v 1.7 2005/10/31 14:37:01 solar Exp $
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

queue *del_set(char *s, queue *q, int *ok);
queue *add_set(char *vv, char *ss, queue *q);

void free_virtuals(queue *list);

/* add a set to a cache */
queue *add_set(char *vv, char *ss, queue *q)
{
	queue *ll, *z;
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
		rmspace(vv);
      
		ll = (queue *) xmalloc(sizeof(queue));
		ll->next = NULL;
		ll->name = (char *) xmalloc(strlen(v) + 1);
      		ll->item = (char *) xmalloc(strlen(s) + 1);
		strcpy(ll->item, s);      
		strcpy(ll->name, v);
      
		if (q == NULL)
			q = ll;
		else {
			z = q;
			while (z->next != NULL)
				z = z->next;
			z->next = ll;
		}

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
queue *del_set(char *s, queue *q, int *ok)
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
void free_sets(queue *list);
void free_sets(queue *list)
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

char *virtual(char *name, queue *list);
char *virtual(char *name, queue *list)
{
	queue *ll;
	for (ll = list; ll != NULL; ll = ll->next)
		if ((strcmp(ll->name, name)) == 0)
			return ll->item;
	return NULL;
}

void print_sets(queue *list);
void print_sets(queue *list)
{
	queue *ll;
	for (ll = list; ll != NULL; ll = ll->next)
		printf("%s -> %s\n", ll->name, ll->item);
}


queue *virtuals = NULL;

static queue *resolve_local_profile_virtuals();
static queue *resolve_local_profile_virtuals() {
	char buf[BUFSIZ];
	FILE *fp;
	char *p;
	char *paths[] = { (char *) "/etc/portage/profile/virtuals", (char *) "/etc/portage/virtuals" };
	size_t i;

	for (i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
		if ((fp = fopen(paths[i], "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				if (*buf != 'v') continue;
				for (p = buf ; *p != 0; ++p) if (isspace(*p)) *p = ' ';
				if ((p = strchr(buf, ' ')) != NULL) {
					int ok = 0;
					*p = 0;
					virtuals = del_set(buf, virtuals, &ok);
					virtuals = add_set(buf, rmspace(++p), virtuals);
					ok = 0;
				}
			}
			fclose(fp);
		}
	}
	return virtuals;
}

static queue *resolve_virtuals();
static queue *resolve_virtuals() {
	static char buf[BUFSIZ];
	static char savecwd[_POSIX_PATH_MAX];
	static char *p;
	FILE *fp;

	memset(buf, 0, sizeof(buf));

	getcwd(savecwd, sizeof(savecwd));

	free_sets(virtuals);
	virtuals = resolve_local_profile_virtuals();

	if ((chdir("/etc/")) == (-1))
		return virtuals;


	if ((readlink("make.profile", buf, sizeof(buf))) != (-1)) {
		chdir(buf);
		getcwd(buf, sizeof(buf));
		if (access(buf, R_OK) != 0)
			return virtuals;
	vstart:
		if ((fp = fopen("virtuals", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				if (*buf != 'v') continue;
				for (p = buf ; *p != 0; ++p) if (isspace(*p)) *p = ' ';
				if ((p = strchr(buf, ' ')) != NULL) {
					*p = 0;
					if (virtual(buf, virtuals) == NULL)
						virtuals = add_set(buf, rmspace(++p), virtuals);
				}
			}
			fclose(fp);
		}
		if ((fp = fopen("parent", "r")) != NULL) {
			while((fgets(buf, sizeof(buf), fp)) != NULL) {
				rmspace(buf);
				if (!*buf) continue;
				if (*buf == '#') continue;
				if (isspace(*buf)) continue;
				fclose(fp);
				if ((chdir(buf)) == (-1)) {
					fclose(fp);
					chdir(savecwd);
					return virtuals;
				}
				goto vstart;
			}
			fclose(fp);
		}
	}
	chdir(savecwd);
	return virtuals;
}
