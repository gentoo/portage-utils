/*
 * Copyright 2010-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

/* our own basename which does not modify its input */
static const char *_basename(const char *filename)
{
	const char *p = strrchr(filename, '/');
	return p ? p + 1 : filename;
}

/* Avoid issues with clobbering C library def */
#undef basename
#define basename(x) _basename(x)
