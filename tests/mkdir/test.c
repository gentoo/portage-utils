/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "tests/tests.h"

#include "libq/xmalloc.c"
#include "libq/xmkdir.c"

int main(int argc, char *argv[])
{
	int i, ret;

	(void)xcalloc;
	(void)xzalloc;
	(void)xrealloc;
	(void)xmemdup;
	(void)rmdir_r_at;

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
