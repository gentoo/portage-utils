/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xreadlink.c,v 1.1 2010/01/13 19:11:03 vapier Exp $
 */

#include <unistd.h>

ssize_t xreadlink(const char *path, char *buf, size_t bufsiz);
ssize_t xreadlink(const char *path, char *buf, size_t bufsiz)
{
	ssize_t ret = readlink(path, buf, bufsiz);
	if (ret == -1)
		errp("readlink(%s) failed", path);
	return ret;
}
