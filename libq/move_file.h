/*
 * Copyright 2005-2021 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _MOVE_FILE_H

#define _MOVE_FILE_H 1

#include <sys/types.h>
#include <sys/stat.h>

int move_file(int rootfd_src, const char *name_src,
			  int rootfd_dst, const char *name_dst,
		  	  struct stat *stat_src);

#endif
