/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _EAT_FILE_H
#define _EAT_FILE_H 1

#include <unistd.h>
#include <stdbool.h>

#include "array.h"

bool eat_file_fd(int, char **, size_t *);
bool eat_file(const char *, char **, size_t *);
bool eat_file_at(int, const char *, char **, size_t *);

array *eat_file_fd_as_array(int fd);
array *eat_file_as_array(const char *name);
array *eat_file_at_as_array(int rootfd, const char *name);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
