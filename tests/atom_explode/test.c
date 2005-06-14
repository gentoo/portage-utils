/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/tests/atom_explode/test.c,v 1.1 2005/06/14 05:09:01 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
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

#define errf(fmt, args...) \
	do { \
	fprintf(stderr, fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)

#include "../../libq/xmalloc.c"
#include "../../libq/atom_explode.c"

int main(int argc, char *argv[])
{
	int i;
	depend_atom *a;
	for (i = 1; i < argc; ++i) {
		a = atom_explode(argv[i]);
		printf("%s -> %s / %s - %s [%s] [r%i]\n",
		       argv[i], a->CATEGORY, a->PN,
		       a->PVR, a->PV, a->PR_int);
		atom_free(a);
	}

	return EXIT_SUCCESS;
}
