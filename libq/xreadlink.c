/*
 * utility funcs
 *
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
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
