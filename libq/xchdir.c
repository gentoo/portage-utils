/*
 * utility funcs
 *
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include <unistd.h>

void xchdir(const char *path);
void xchdir(const char *path)
{
	if (unlikely(chdir(path) != 0))
		errp("chdir(%s) failed", path);
}
