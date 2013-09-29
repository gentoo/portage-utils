/*
 * Copyright 2005-2013 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/tests/mkdir/test.c,v 1.3 2013/09/29 22:42:36 vapier Exp $
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2013 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "tests/tests.h"

#include "libq/xmalloc.c"
#include "libq/xstrdup.c"
#include "libq/xmkdir.c"

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
