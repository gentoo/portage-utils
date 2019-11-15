/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _XPAK_H
#define _XPAK_H 1

typedef void (*xpak_callback_t)(void *, char *, int, int, int, char *);

int xpak_process_fd(int, bool, void *, xpak_callback_t);
int xpak_process(const char *, bool, void *, xpak_callback_t);
#define xpak_list(A,B,C)    xpak_process(A,false,B,C)
#define xpak_extract(A,B,C) xpak_process(A,true,B,C)
int xpak_create(int, const char *, int, char **, bool, int);

#endif
