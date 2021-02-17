/*
 * Copyright 2021 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2021-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>

#include "atom.h"
#include "tree.h"

#define QWHICH_FLAGS "IbtpdfF:" COMMON_FLAGS
static struct option const qwhich_long_opts[] = {
	{"vdb",      no_argument, NULL, 'I'},
	{"binpkg",   no_argument, NULL, 'b'},
	{"tree",     no_argument, NULL, 't'},
	{"pretty",   no_argument, NULL, 'p'},
	{"dir",      no_argument, NULL, 'd'},
	{"first",    no_argument, NULL, 'f'},
	{"format",    a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const qwhich_opts_help[] = {
	"Look in VDB (installed packages)",
	"Look at binary packages",
	"Look in main tree and overlays",
	"Print (pretty) atom instead of path for use with -F",
	"Print directory instead of path",
	"Stop searching after first match",
	"Print matched using given format string",
	COMMON_OPTS_HELP
};
static const char qwhich_desc[] = "";
#define qwhich_usage(ret) \
	usage(ret, QWHICH_FLAGS, qwhich_long_opts, qwhich_opts_help, qwhich_desc, lookup_applet_idx("qwhich"))

struct qwhich_mode {
	char do_vdb:1;
	char do_binpkg:1;
	char do_tree:1;
	char print_atom:1;
	char print_path:1;
	char match_first:1;
	const char *fmt;
};

int qwhich_main(int argc, char **argv)
{
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	DECLARE_ARRAY(trees);
	struct qwhich_mode m;
	struct tree_match_ctx *tmc;
	struct tree_match_ctx *tmcw;
	size_t i;
	size_t j;
	char *overlay;
	size_t n;
	int ret;
	tree_ctx *t;
	int repolen;

	memset(&m, 0, sizeof(m));

	while ((ret = GETOPT_LONG(QWHICH, qwhich, "")) != -1) {
		switch (ret) {
			COMMON_GETOPTS_CASES(qwhich)

			case 'I': m.do_vdb = true;      break;
			case 'b': m.do_binpkg = true;   break;
			case 't': m.do_tree = true;     break;
			case 'p': m.print_atom = true;  break;
			case 'd': m.print_path = true;  break;
			case 'f': m.match_first = true; break;
			case 'F': m.fmt = optarg;       break;
		}
	}

	/* defaults: no options at all, enable first match,
	 *           no selectors, enable tree + overlays */
	if (!m.do_vdb && !m.do_binpkg && !m.do_tree) {
		if (!m.print_atom && !m.print_path && !m.match_first && m.fmt == NULL)
			m.match_first = true;
		m.do_tree = true;
	}

	/* when printing path, we better just match the first, else we get a
	 * lot of dups */
	if (m.print_path)
		m.match_first = true;

	/* set format if none given */
	if (m.fmt == NULL) {
		if (verbose)
			m.fmt = "%[CATEGORY]%[PF]";
		else
			m.fmt = "%[CATEGORY]%[PN]";
	} else {
		/* makes no sense to use formatter if we're not going to use it */
		m.print_atom = true;
	}

	argc -= optind;
	argv += optind;
	for (i = 0; i < (size_t)argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom)
			warn("invalid atom: %s", argv[i]);
		else
			xarraypush_ptr(atoms, atom);
	}

	/* TODO: silence when the path doesn't exist -- reasonable though? */
	if (m.do_vdb) {
		t = tree_open_vdb(portroot, portvdb);
		if (t != NULL)
			xarraypush_ptr(trees, t);
	}
	if (m.do_binpkg) {
		t = tree_open_binpkg(portroot, pkgdir);
		if (t != NULL)
			xarraypush_ptr(trees, t);
	}

	if (m.do_tree) {
		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL)
				xarraypush_ptr(trees, t);
		}
	}

	/* at least keep the IO constrained to a tree at a time */
	array_for_each(trees, j, t) {
		if (t->cachetype == CACHE_METADATA_MD5)
			repolen = strlen(t->path) - (sizeof("/metadata/md5-cache") - 1);
		else if (t->cachetype == CACHE_METADATA_PMS)
			repolen = strlen(t->path) - (sizeof("/metadata/cache") - 1);
		else
			repolen = 0;

		array_for_each(atoms, i, atom) {
			tmc = tree_match_atom(t, atom,
					m.match_first ? TREE_MATCH_FIRST : TREE_MATCH_DEFAULT);
			for (tmcw = tmc; tmcw != NULL; tmcw = tmcw->next) {
				if (m.print_atom) {
					printf("%s\n", atom_format(m.fmt, tmcw->atom));
				} else {
					if (t->cachetype == CACHE_METADATA_MD5 ||
							t->cachetype == CACHE_METADATA_PMS)
					{
						if (m.print_path)
							printf("%s%.*s/%s%s/%s%s%s\n",
									GREEN, repolen, t->path,
									BOLD, tmcw->atom->CATEGORY,
									DKBLUE, tmcw->atom->PN,
									NORM);
						else
							printf("%s%.*s/%s%s/%s%s/%s%s%s.ebuild%s\n",
									DKGREEN, repolen, t->path,
									BOLD, tmcw->atom->CATEGORY,
									DKBLUE, tmcw->atom->PN,
									BLUE, tmcw->atom->P,
									DKGREEN, NORM);
					} else {
						printf("%s%s%s\n", DKBLUE, tmcw->path, NORM);
					}
				}
			}
			tree_match_close(tmc);
		}
		tree_close(t);
	}
	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	xarrayfree_int(trees);

	return EXIT_SUCCESS;
}
