/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "tests/tests.h"

#include "libq/xmalloc.c"
#include "libq/atom_explode.c"

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

	(void)xcalloc;
	(void)xmemdup;

	/* printf("input -> CATEGORY / [P] PN - PVR [PV] [PR_int]\n"); */
	for (i = 1; i < argc; ++i) {
		a = atom_explode(argv[i]);
		boom(a,argv[i]);
		atom_implode(a);
	}
	if (argc == 1) {
		size_t buflen;
		char *buf, *p;

		buf = NULL;
		while (getline(&buf, &buflen, stdin) != -1) {
			if ((p = strchr(buf, '\n')) != NULL)
				*p = '\0';
			a = atom_explode(buf);
			boom(a, buf);
			atom_implode(a);
		}
	}

	return EXIT_SUCCESS;
}
