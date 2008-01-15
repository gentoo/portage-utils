/*
 * Copyright 2005-2008 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/tests/atom_explode/test.c,v 1.9 2008/01/15 08:06:11 vapier Exp $
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2008 Mike Frysinger  - <vapier@gentoo.org>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>

#define warnf(fmt, args...) fprintf(stderr, fmt "\n", ## args)
#define errf(fmt, args...) \
	do { \
	warnf(fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#define err(...) errf(__VA_ARGS__)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

#include "../../libq/xmalloc.c"
#include "../../libq/atom_explode.c"

static inline void boom(depend_atom *a, char *s)
{
	printf("%s -> %s / [%s] %s - %s [%s] [r%i]\n",
	       s, (a->CATEGORY?:"null"), a->P, a->PN,
	       a->PVR, a->PV, a->PR_int);
}

int main(int argc, char *argv[])
{
	int i;
	depend_atom *a;
	/* printf("input -> CATEGORY / [P] PN - PVR [PV] [PR_int]\n"); */
	for (i = 1; i < argc; ++i) {
		a = atom_explode(argv[i]);
		boom(a,argv[i]);
		atom_implode(a);
	}
	if (argc == 1) {
		char buf[1024], *p;
		while (fgets(buf, sizeof(buf), stdin) != NULL) {
			if ((p = strchr(buf, '\n')) != NULL)
				*p = '\0';
			a = atom_explode(buf);
			boom(a,buf);
			atom_implode(a);
		}
	}

	return EXIT_SUCCESS;
}
