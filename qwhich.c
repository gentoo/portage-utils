/*
 * Copyright 2021-2026 Gentoo Foundation
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

#define QWHICH_FLAGS "Ibtpdr:RflTAF:" COMMON_FLAGS
static struct option const qwhich_long_opts[] = {
	{"vdb",       no_argument, NULL, 'I'},
	{"binpkg",    no_argument, NULL, 'b'},
	{"tree",      no_argument, NULL, 't'},
	{"pretty",    no_argument, NULL, 'p'},
	{"dir",       no_argument, NULL, 'd'},
	{"repo",       a_argument, NULL, 'r'},
	{"printrepo", no_argument, NULL, 'R'},
	{"first",     no_argument, NULL, 'f'},
	{"latest",    no_argument, NULL, 'l'},
	{"novirtual", no_argument, NULL, 'T'},
	{"noacct",    no_argument, NULL, 'A'},
	{"format",     a_argument, NULL, 'F'},
	COMMON_LONG_OPTS
};
static const char * const qwhich_opts_help[] = {
	"Look in VDB (installed packages)",
	"Look at binary packages",
	"Look in main tree and overlays",
	"Print (pretty) atom instead of path for use with -F",
	"Print directory instead of path",
	"Only look in given repo",
	"Print repository name instead of path for tree/overlay matches",
	"Stop searching after first match (implies -l)",
	"Only return latest version for each match",
	"Skip virtual category",
	"Skip acct-user and acct-group categories",
	"Print matched using given format string",
	COMMON_OPTS_HELP
};
static const char qwhich_desc[] = "Find paths to ebuilds.";
#define qwhich_usage(ret) \
	usage(ret, QWHICH_FLAGS, qwhich_long_opts, qwhich_opts_help, qwhich_desc, lookup_applet_idx("qwhich"))

struct qwhich_mode {
	char do_vdb:1;
	char do_binpkg:1;
	char do_tree:1;
	char print_atom:1;
	char print_path:1;
	char print_repo:1;
	char match_first:1;
	char match_latest:1;
	char skip_virtual:1;
	char skip_acct:1;
	const char *fmt;
};

int qwhich_main(int argc, char **argv)
{
	depend_atom *atom;
	array *atoms = array_new();
	array *trees = array_new();
	struct qwhich_mode m;
	array *tmc;
	tree_pkg_ctx *tmcw;
	size_t i;
	size_t j;
	char *overlay;
	size_t n;
	int ret;
	tree_ctx *t;
	char *repo = NULL;
	char *reponam;
	int repolen;

	VAL_CLEAR(m);

	while ((ret = GETOPT_LONG(QWHICH, qwhich, "")) != -1) {
		switch (ret) {
			COMMON_GETOPTS_CASES(qwhich)

			case 'I': m.do_vdb = true;       break;
			case 'b': m.do_binpkg = true;    break;
			case 't': m.do_tree = true;      break;
			case 'p': m.print_atom = true;   break;
			case 'd': m.print_path = true;   break;
			case 'r': repo = optarg;         break;
			case 'R': m.print_repo = true;   break;
			case 'f': m.match_first = true;  break;
			case 'l': m.match_latest = true; break;
			case 'T': m.skip_virtual = true; break;
			case 'A': m.skip_acct = true;    break;
			case 'F': m.fmt = optarg;        break;
		}
	}

	/* defaults: no selectors, enable tree + overlays */
	if (!m.do_vdb && !m.do_binpkg && !m.do_tree)
		m.do_tree = true;

	/* when printing path, we better just match the first, else we get a
	 * lot of dups */
	if (m.print_path || m.match_first)
		m.match_latest = true;

	/* set format if none given */
	if (m.fmt == NULL) {
		if (verbose)
			m.fmt = "%[CATEGORY]%[PF]";
		else
			m.fmt = "%[CATEGORY]%[PN]";
		/* pointless to report duplicates */
		if (m.print_atom &&
			!verbose)
			m.match_latest = true;
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
			array_append(atoms, atom);
	}

	/* TODO: silence when the path doesn't exist -- reasonable though? */
	if (m.do_vdb) {
		t = tree_open_vdb(portroot, portvdb);
		if (t != NULL)
			array_append(trees, t);
	}
	if (m.do_binpkg) {
		t = tree_open_binpkg("/", pkgdir);
		if (t != NULL)
			array_append(trees, t);
	}

	if (m.do_tree) {
		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL)
				array_append(trees, t);
		}
	}

	/* at least keep the IO constrained to a tree at a time */
	array_for_each(trees, j, t) {
		char *reponame = tree_get_repo_name(t);
		if (repo != NULL &&
			(reponame == NULL || strcmp(repo, reponame) != 0))
		{
			tree_close(t);
			continue;
		}

		if (m.print_repo && reponame != NULL) {
			reponam = reponame;
			repolen = strlen(reponam);
		} else {
			reponam = tree_get_path(t);
			repolen = strlen(reponam);
		}

		array_for_each(atoms, i, atom) {
			/* because this is for direct visuals, always require sorted
			 * order so the output is consistent and predictable */
			tmc = tree_match_atom(t, atom, TREE_MATCH_SORT |
					(m.match_latest ? TREE_MATCH_LATEST : 0 ) |
					(m.match_first  ? TREE_MATCH_FIRST  : 0 ) |
					(m.skip_virtual ? 0 : TREE_MATCH_VIRTUAL) |
					(m.skip_acct    ? 0 : TREE_MATCH_ACCT   ));
			array_for_each(tmc, n, tmcw)
			{
				if (m.print_atom) {
					printf("%s\n", atom_format(m.fmt,
											   tree_pkg_atom(tmcw, false)));
				} else {
					char  *path = tree_pkg_get_path(tmcw) + 1;
					size_t len  = strlen(path);

					if (len > repolen)
					{
						path += repolen;
						len  -= repolen;
					}

					if (m.print_path)
					{
						char *p = strrchr(path, '/');
						if (p == NULL)
							len = 0;
						else
							len = p - path;
					}

					printf("%s%s%.*s%s%s%.*s%s\n",
						   GREEN, m.print_repo ? "" : "/",
						   repolen, reponam, m.print_repo ? "::" : "/",
						   DKBLUE, (int)len, path, NORM);
				}
			}
			array_free(tmc);
		}
		tree_close(t);
	}
	array_deepfree(atoms, (array_free_cb *)atom_implode);
	array_free(trees);

	return EXIT_SUCCESS;
}
