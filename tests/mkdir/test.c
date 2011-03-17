/*
 * Copyright 2005-2008 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/tests/mkdir/test.c,v 1.2 2011/03/17 01:57:27 vapier Exp $
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2008 Mike Frysinger  - <vapier@gentoo.org>
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
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
#define errp(...) errf(__VA_ARGS__)
#define _q_static static

#include "../../libq/xmalloc.c"
#include "../../libq/xstrdup.c"
#include "../../libq/xmkdir.c"

int main(int argc, char *argv[])
{
	int i, ret;

	if (argc <= 1)
		return 1;

	ret = 0;

	if (!strcmp(argv[1], "m")) {
		for (i = 2; i < argc; ++i)
			ret += mkdir_p(argv[i], 0755);
	} else if (!strcmp(argv[1], "rm")) {
		for (i = 2; i < argc; ++i)
			ret += rm_rf(argv[i]);
	}

	return ret;
}
