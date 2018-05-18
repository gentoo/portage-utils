/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _QAPPLETS_H_
#define _QAPPLETS_H_

/* applet prototypes */
typedef int (*APPLET)(int, char **);

#define DECLARE_APPLET(applet) \
	extern int applet##_main(int, char **) __attribute__((weak));
DECLARE_APPLET(q)
DECLARE_APPLET(qcheck)
DECLARE_APPLET(qdepends)
DECLARE_APPLET(qfile)
DECLARE_APPLET(qlist)
DECLARE_APPLET(qlop)
DECLARE_APPLET(qsearch)
DECLARE_APPLET(qsize)
DECLARE_APPLET(qtbz2)
DECLARE_APPLET(quse)
DECLARE_APPLET(qxpak)
DECLARE_APPLET(qpkg)
DECLARE_APPLET(qgrep)
DECLARE_APPLET(qatom)
DECLARE_APPLET(qmerge)
DECLARE_APPLET(qcache)
DECLARE_APPLET(qglsa) /* disable */
DECLARE_APPLET(qtegrity)
#undef DECLARE_APPLET

#define DEFINE_APPLET_STUB(applet) \
	int applet##_main(_q_unused_ int argc, _q_unused_ char **argv) { \
		err("Sorry, this applet has been disabled"); \
	}

static const struct applet_t {
	const char *name;
	APPLET func;
	const char *opts;
	const char *desc;
} applets[] = {
	/* q must always be the first applet */
	{"q",         q_main,         "<applet> <args>", "virtual applet"},
	{"qatom",     qatom_main,     "<pkg>",           "split atom strings"},
	{"qcache",    qcache_main,    "<action> <args>", "search the metadata cache"},
	{"qcheck",    qcheck_main,    "<pkgname>",       "verify integrity of installed packages"},
	{"qdepends",  qdepends_main,  "<pkgname>",       "show dependency info"},
	{"qfile",     qfile_main,     "<filename>",      "list all pkgs owning files"},
	{"qglsa",     qglsa_main,     "<action> <list>", "check GLSAs against system"},
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
	{"qtegrity",  qtegrity_main,  "<misc args>",     "verify files with IMA"},

	/* aliases for equery compatibility */
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
	/* {"glsa",      qglsa_main,     NULL, NULL}, */

	/* alias for qtegrity */
	{"integrity", qtegrity_main,  NULL, NULL},

	{NULL, NULL, NULL, NULL}
};

#endif
