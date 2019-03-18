/*
 * Copyright 2011-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _XMKDIR_H
#define _XMKDIR_H 1

int mkdir_p_at(int dfd, const char *path, mode_t mode);
int mkdir_p(const char *path, mode_t mode);
int rm_rf_at(int dfd, const char *path);
int rm_rf(const char *path);
int rmdir_r_at(int dfd, const char *path);
int rmdir_r(const char *path);

#endif
