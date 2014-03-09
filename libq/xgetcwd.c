/*
 * utility funcs
 *
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include <unistd.h>

char *xgetcwd(char *buf, size_t size);
char *xgetcwd(char *buf, size_t size)
{
	char *ret = getcwd(buf, size);
	if (!ret)
		errp("getcwd() failed");
	return ret;
}
