/*
 * Copyright 2005-2013 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/tests/tests.h,v 1.1 2013/09/29 22:42:36 vapier Exp $
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2013 Mike Frysinger  - <vapier@gentoo.org>
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
