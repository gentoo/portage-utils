/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xgetcwd.c,v 1.1 2010/01/13 18:48:01 vapier Exp $
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
