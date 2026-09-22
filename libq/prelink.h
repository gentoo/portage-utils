/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _PRELINK_H
#define _PRELINK_H 1

#include <stdbool.h>

bool prelink_available(void);
int  prelink_undo_hash_cb(int fd, const char *filename);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
