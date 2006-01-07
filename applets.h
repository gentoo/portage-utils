/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/applets.h,v 1.12 2006/01/07 16:25:28 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
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
int qgrep_main(int, char **);
int qatom_main(int, char **);
int qmerge_main(int, char **);

typedef int (*APPLET)(int, char **);

struct applet_t {
	const char *name;
	/* int *func; */
	APPLET func;
	const char *opts;
	const char *desc;
} applets[] = {
	/* q must always be the first applet */
	{"q",         q_main,         "<applet> <args>", "virtual applet"},
	{"qatom",     qatom_main,     "<pkg>",           "split atom strings"},
	{"qcheck",    qcheck_main,    "<pkgname>",       "verify mtimes/digests"},
	{"qdepends",  qdepends_main,  "<pkgname>",       "show dependency info"},
	{"qfile",     qfile_main,     "<filename>",      "list all pkgs owning files"},
	{"qgrep",     qgrep_main,     "<misc args>",     "grep in ebuilds"},
	{"qlist",     qlist_main,     "<pkgname>",       "list files owned by pkgname"},
	{"qlop",      qlop_main,      "<pkgname>",       "emerge log analyzer"},
	{"qmerge",    qmerge_main,    "<pkgnames>",      "fetch and merge binary package"},
	{"qpkg",      qpkg_main,      "<misc args>",     "manipulate Gentoo binpkgs"},
	{"qsearch",   qsearch_main,   "<regex>",         "search pkgname/desc"},
	{"qsize",     qsize_main,     "<pkgname>",       "calculate size usage"},
	{"qtbz2",     qtbz2_main,     "<misc args>",     "manipulate tbz2 packages"},
	{"quse",      quse_main,      "<useflag>",       "find pkgs using useflags"},
	{"qxpak",     qxpak_main,     "<misc args>",     "manipulate xpak archives"},


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
