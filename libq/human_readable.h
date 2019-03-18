/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _HUMAN_READABLE_H
#define _HUMAN_READABLE_H 1

enum {
	KILOBYTE = 1024,
	MEGABYTE = (KILOBYTE*1024),
	GIGABYTE = (MEGABYTE*1024)
};

const char *make_human_readable_str(
		unsigned long long val,
		unsigned long block_size,
		unsigned long display_unit);

#endif
