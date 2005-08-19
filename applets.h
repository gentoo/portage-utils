/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/applets.h,v 1.4 2005/08/19 03:47:01 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */

#ifndef _QAPPLETS_H_
#define _QAPPLETS_H_

/* applet prototypes */
int q_main(int, char **);
int qcheck_main(int, char **);
int qdepends_main(int, char **);
int qfile_main(int, char **);
int qlist_main(int, char **);
int qlop_main(int, char **);
int qsearch_main(int, char **);
int qsize_main(int, char **);
int qtbz2_main(int, char **);
int quse_main(int, char **);
int qxpak_main(int, char **);
int qpkg_main(int, char **);

typedef int (*APPLET)(int, char **);

/* applets we support */
typedef enum {
	FIRST_APPLET = 0,
	APPLET_Q = 0,
	APPLET_QCHECK = 1,
	APPLET_QDEPENDS = 2,
	APPLET_QFILE = 3,
	APPLET_QLIST = 4,
	APPLET_QLOP = 5,
	APPLET_QSEARCH = 6,
	APPLET_QSIZE = 7,
	APPLET_QTBZ2 = 8,
	APPLET_QUSE = 9,
	APPLET_QXPAK = 10,
	APPLET_QPKG = 11,
	LAST_APPLET = 11
} applets_enum;

struct applet_t {
	const char *name;
	/* int *func; */
	APPLET func;
	const char *opts;
	const char *desc;
} applets[] = {
	/* q must always be the first applet */
	{"q",         q_main,         "<applet> <args>", "virtual applet"},
	{"qcheck",    qcheck_main,    "<pkgname>",       "verify mtimes/digests"},
	{"qdepends",  qdepends_main,  "<pkgname>",       "show dependency info"},
	{"qfile",     qfile_main,     "<filename>",      "list all pkgs owning files"},
	{"qlist",     qlist_main,     "<pkgname>",       "list files owned by pkgname"},
	{"qlop",      qlop_main,      "<pkgname>",       "emerge log analyzer"},
	{"qsearch",   qsearch_main,   "<regex>",         "search pkgname/desc"},
	{"qsize",     qsize_main,     "<pkgname>",       "calculate size usage"},
	{"qtbz2",     qtbz2_main,     "<misc args>",     "manipulate tbz2 packages"},
	{"quse",      quse_main,      "<useflag>",       "find pkgs using useflags"},
	{"qxpak",     qxpak_main,     "<misc args>",     "manipulate xpak archives"},
	{"qpkg",      qpkg_main,      "<misc args>",     "manipulate Gentoo binpkgs"},


	/* aliases for equery capatability */
	{"belongs",   qfile_main,     NULL, NULL},
	/*"changes"*/
	{"check",     qcheck_main,    NULL, NULL},
	{"depends",   qdepends_main,  NULL, NULL},
	/*"depgraph"*/
	{"files",     qlist_main,     NULL, NULL},
	/*"glsa"*/
	{"hasuse",    quse_main,      NULL, NULL},
	/*"list"*/
	{"size",      qsize_main,     NULL, NULL},
	/*"stats"*/
	/*"uses"*/
	/*"which"*/

	/* alias for quickpkg */
	{"uickpkg",   qpkg_main,      NULL, NULL},

	{NULL, NULL, NULL, NULL}
};

#endif
