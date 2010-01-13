/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xchdir.c,v 1.1 2010/01/13 18:17:26 vapier Exp $
 */

#include <unistd.h>

void xchdir(const char *path);
void xchdir(const char *path)
{
	if (chdir(path))
		errp("chdir(%s) failed", path);
}
