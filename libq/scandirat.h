/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
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
int filter_hidden(const struct dirent *dentry);

#endif
