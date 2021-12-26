/*
 * Copyright 2005-2021 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2021-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _SCANDIRAT_H
#define _SCANDIRAT_H 1

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#if !defined(HAVE_SCANDIRAT)
int scandirat(
		int dir_fd, const char *dir, struct dirent ***dirlist,
		int (*filter)(const struct dirent *),
		int (*compar)(const struct dirent **, const struct dirent **));
#endif

void scandir_free(struct dirent **de, int cnt);
int filter_hidden(const struct dirent *de);
int filter_self_parent(const struct dirent *de);

#endif
