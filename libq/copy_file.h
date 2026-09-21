/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _COPY_FILE_H
#define _COPY_FILE_H 1

#include <stdio.h>

int copy_file_fd(int fd_src, int fd_dst);
int copy_file(FILE *src, FILE *dst);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
