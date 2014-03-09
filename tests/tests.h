/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _TESTS_H
#define _TESTS_H

#include "porting.h"

#define warnf(fmt, args...) fprintf(stderr, fmt "\n", ## args)
#define errf(fmt, args...) \
	do { \
	warnf(fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#define err(...) errf(__VA_ARGS__)
#define errp(...) errf(__VA_ARGS__)
#define _q_static static

#endif
