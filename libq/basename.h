/*
 * Copyright 2010-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _BASENAME_H
#define _BASENAME_H 1

const char *_basename(const char *filename);

/* Avoid issues with clobbering C library def */
#undef basename
#define basename(x) _basename(x)

#endif
