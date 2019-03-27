/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _XPAK_H
#define _XPAK_H 1

typedef void (*xpak_callback_t)(int,char*,int,int,int,char*);

int xpak_list(
		int dir_fd,
		const char *file,
		int argc,
		char **argv,
		xpak_callback_t func);
int xpak_extract(
	int dir_fd,
	const char *file,
	int argc,
	char **argv,
	xpak_callback_t func);
int xpak_create(
		int dir_fd,
		const char *file,
		int argc,
		char **argv,
		char append,
		int v);

#endif
