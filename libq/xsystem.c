/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xsystem.c,v 1.1 2010/01/13 19:15:17 vapier Exp $
 */

#include <stdlib.h>

void xsystem(const char *command);
void xsystem(const char *command)
{
	if (system(command))
		errp("system(%s) failed", command);
}
