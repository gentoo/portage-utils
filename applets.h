/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _APPLETS_H_
#define _APPLETS_H_ 1

#if defined(__sun) && defined(__SVR4)
/* workaround non-const defined name in option struct, such that we
 * don't get a zillion of warnings */
#define	no_argument		0
#define	required_argument	1
#define	optional_argument	2
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};
extern int	getopt_long(int, char * const *, const char *,
		    const struct option *, int *);
#else
# include <getopt.h>
#endif

#include <stdbool.h>
#include <dirent.h>

#include "xarray.h"

/* applet prototypes */
typedef int (*APPLET)(int, char **);

#define DECLARE_APPLET(applet) extern int applet##_main(int, char **);
DECLARE_APPLET(q)
DECLARE_APPLET(qatom)
DECLARE_APPLET(qcheck)
DECLARE_APPLET(qdepends)
DECLARE_APPLET(qfile)
/*DECLARE_APPLET(qglsa) disable */
DECLARE_APPLET(qgrep)
DECLARE_APPLET(qkeyword)
DECLARE_APPLET(qlist)
DECLARE_APPLET(qlop)
#ifdef HAVE_QMANIFEST
DECLARE_APPLET(qmanifest)
#endif
DECLARE_APPLET(qmerge)
DECLARE_APPLET(qpkg)
DECLARE_APPLET(qsearch)
DECLARE_APPLET(qsize)
DECLARE_APPLET(qtbz2)
DECLARE_APPLET(qtegrity)
DECLARE_APPLET(quse)
DECLARE_APPLET(qxpak)
#undef DECLARE_APPLET

static const struct applet_t {
	const char *name;
	APPLET func;
	const char *opts;
	const char *desc;
} applets[] = {
	/* q must always be the first applet */
	{"q",         q_main,         "<applet> <args>", "virtual applet"},
	{"qatom",     qatom_main,     "<pkg>",           "split atom strings"},
	{"qcheck",    qcheck_main,    "<pkgname>",       "verify integrity of installed packages"},
	{"qdepends",  qdepends_main,  "<pkgname>",       "show dependency info"},
	{"qfile",     qfile_main,     "<filename>",      "list all pkgs owning files"},
	/*
	{"qglsa",     qglsa_main,     "<action> <list>", "check GLSAs against system"},
	*/
	{"qgrep",     qgrep_main,     "<expr> [pkg ...]", "grep in ebuilds"},
	{"qkeyword",  qkeyword_main,  "<action> <args>", "list packages based on keywords"},
	{"qlist",     qlist_main,     "<pkgname>",       "list files owned by pkgname"},
	{"qlop",      qlop_main,      "<pkgname>",       "emerge log analyzer"},
#ifdef HAVE_QMANIFEST
	{"qmanifest", qmanifest_main, "<misc args>",     "verify or generate thick Manifest files"},
#endif
	{"qmerge",    qmerge_main,    "<pkgnames>",      "fetch and merge binary package"},
	{"qpkg",      qpkg_main,      "<misc args>",     "manipulate Gentoo binpkgs"},
	{"qsearch",   qsearch_main,   "<regex>",         "search pkgname/desc"},
	{"qsize",     qsize_main,     "<pkgname>",       "calculate size usage"},
	{"qtbz2",     qtbz2_main,     "<misc args>",     "manipulate tbz2 packages"},
	{"qtegrity",  qtegrity_main,  "<misc args>",     "verify files with IMA"},
	{"quse",      quse_main,      "<useflag>",       "find pkgs using useflags"},
	{"qxpak",     qxpak_main,     "<misc args>",     "manipulate xpak archives"},

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

#ifdef HAVE_QMANIFEST
	/* old hashgen */
	{"hashgen",   qmanifest_main, NULL, NULL},
	{"hashverify",qmanifest_main, NULL, NULL},
#endif

	{NULL, NULL, NULL, NULL}
};

/* Common usage for all applets */
#define COMMON_FLAGS "vqChV"
#define COMMON_LONG_OPTS \
	{"root",       a_argument, NULL, 0x1}, \
	{"verbose",   no_argument, NULL, 'v'}, \
	{"quiet",     no_argument, NULL, 'q'}, \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Set the ROOT env var", \
	"Make a lot of noise", \
	"Tighter output; suppress warnings", \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 0x1: portroot = optarg; break; \
	case 'v': ++verbose; break; \
	case 'q': setup_quiet(); break; \
	case 'V': version_barf(); break; \
	case 'h': applet ## _usage(EXIT_SUCCESS); break; \
	case 'C': no_colors(); break; \
	default: applet ## _usage(EXIT_FAILURE); break;

extern char *portroot;
extern int verbose;
extern int quiet;
extern char pretend;
extern char *config_protect;
extern char *config_protect_mask;
extern char *portvdb;
extern char *portlogdir;
extern char *pkg_install_mask;
extern char *binhost;
extern char *pkgdir;
extern char *port_tmpdir;
extern char *features;
extern char *install_mask;
extern DEFINE_ARRAY(overlays);
extern DEFINE_ARRAY(overlay_names);
extern char *main_overlay;

void no_colors(void);
void setup_quiet(void);
void version_barf(void);
void usage(int status, const char *flags, struct option const opts[],
      const char * const help[], const char *desc, int blabber);
int lookup_applet_idx(const char *);
APPLET lookup_applet(const char *applet);
void freeargv(int argc, char **argv);
void makeargv(const char *string, int *argc, char ***argv);

#endif
