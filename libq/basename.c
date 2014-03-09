/*
 * Copyright 2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

static const char *_basename(const char *filename)
{
	const char *p = strrchr(filename, '/');
	return p ? p + 1 : filename;
}

/* Avoid issues with clobbering C library def */
#undef basename
#define basename(x) _basename(x)
