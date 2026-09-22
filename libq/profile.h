/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _PROFILE_H
#define _PROFILE_H 1

typedef void *(profile_cb_t)(void *data, char *row);

void *profile_walk_rows(const char *file, profile_cb_t callback, void *data);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
