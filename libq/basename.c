/*
 * Copyright 2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/basename.c,v 1.1 2010/07/19 00:25:13 vapier Exp $
 */

static const char *_basename(const char *filename)
{
	const char *p = strrchr(filename, '/');
	return p ? p + 1 : filename;
}

/* Avoid issues with clobbering C library def */
#undef basename
#define basename(x) _basename(x)
