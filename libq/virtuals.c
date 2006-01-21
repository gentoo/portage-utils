/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/virtuals.c,v 1.11 2006/01/21 23:31:24 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 *
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/virtuals.c,v 1.11 2006/01/21 23:31:24 solar Exp $
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

/* global */
queue *virtuals = NULL;

queue *del_set(char *s, queue *q, int *ok);
queue *add_set(const char *vv, const char *ss, queue *q);

void free_virtuals(queue *list);

/* add a set to a cache */
queue *add_set(const char *vv, const char *ss, queue *q)
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
		rmspace(v);

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

queue *resolve_vdb_virtuals(char *vdb);
queue *resolve_vdb_virtuals(char *vdb) {
        DIR *dir, *dirp;
        struct dirent *dentry_cat, *dentry_pkg;
        char buf[BUFSIZE];
        depend_atom *atom;

	chdir("/");

        /* now try to run through vdb and locate matches for user inputs */
        if ((dir = opendir(vdb)) == NULL)
                return virtuals;

        /* scan all the categories */
        while ((dentry_cat = q_vdb_get_next_dir(dir)) != NULL) {
                snprintf(buf, sizeof(buf), "%s/%s", vdb, dentry_cat->d_name);
                if ((dirp = opendir(buf)) == NULL)
                        continue;

                /* scan all the packages in this category */
                while ((dentry_pkg = q_vdb_get_next_dir(dirp)) != NULL) {
			FILE *fp;
			char *p;
                        /* see if user wants any of these packages */
			snprintf(buf, sizeof(buf), "%s/%s/%s/PROVIDE", vdb, dentry_cat->d_name, dentry_pkg->d_name);
			if ((fp = fopen(buf, "r")) != NULL) {
				fgets(buf, sizeof(buf), fp);

				if ((p = strrchr(buf, '\n')) != NULL)
					*p = 0;

				rmspace(buf);

				if (*buf) {
					int ok = 0;
					char *v, *tmp = xstrdup(buf);
		                        snprintf(buf, sizeof(buf), "%s/%s", dentry_cat->d_name, dentry_pkg->d_name);

					atom = atom_explode(buf);
					if (!atom) {
						warn("could not explode '%s'", buf);
						continue;
                        		}
					sprintf(buf, "%s/%s", atom->CATEGORY, atom->PN);
					if ((v = virtual(tmp, virtuals)) != NULL) {
						// IF_DEBUG(fprintf(stderr, "%s provided by %s (removing)\n", tmp, v));
						virtuals = del_set(tmp,  virtuals, &ok);
					}
					virtuals = add_set(tmp, buf, virtuals);
					// IF_DEBUG(fprintf(stderr, "%s provided by %s/%s (adding)\n", tmp, atom->CATEGORY, dentry_pkg->d_name));
					free(tmp);
					atom_implode(atom);
				}
				fclose(fp);
			}
                }
        }
	return virtuals;
}

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
	// virtuals = resolve_vdb_virtuals(portvdb);

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

