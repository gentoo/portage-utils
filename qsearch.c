/*
 * Copyright 2005-2019 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <xalloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "atom.h"
#include "basename.h"
#include "rmspace.h"
#include "tree.h"
#include "xarray.h"
#include "xregex.h"

#define QSEARCH_FLAGS "asSNHR" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"all",       no_argument, NULL, 'a'},
	{"search",    no_argument, NULL, 's'},
	{"desc",       a_argument, NULL, 'S'},
	{"name-only", no_argument, NULL, 'N'},
	{"homepage",  no_argument, NULL, 'H'},
	{"repo",      no_argument, NULL, 'R'},
	COMMON_LONG_OPTS
};
static const char * const qsearch_opts_help[] = {
	"List the descriptions of every package in the cache",
	"Regex search package basenames",
	"Regex search package descriptions (or homepage when using -H)",
	"Only show package name",
	"Show homepage info instead of description",
	"Show repository the ebuild originates from",
	COMMON_OPTS_HELP
};
#define qsearch_usage(ret) usage(ret, QSEARCH_FLAGS, qsearch_long_opts, qsearch_opts_help, NULL, lookup_applet_idx("qsearch"))

struct qsearch_state {
	bool show_homepage:1;
	bool show_name:1;
	bool show_desc:1;
	bool show_repo:1;
	bool search_desc:1;
	bool search_name:1;
	regex_t search_expr;
};

static int
qsearch_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	static depend_atom *last_atom;

	struct qsearch_state *state = (struct qsearch_state *)priv;
	depend_atom *atom;
	tree_pkg_meta *meta;
	char *desc;
	bool match;

	atom = tree_get_atom(pkg_ctx, 0);
	if (atom == NULL)
		return 0;

	/* skip duplicate packages (we never report version) */
	if (last_atom != NULL && strcmp(last_atom->PN, atom->PN) == 0)
		return 0;

	match = false;
	if (state->search_name &&
			regexec(&state->search_expr, atom->PN, 0, NULL, 0) == 0)
		match = true;

	desc = NULL;
	meta = NULL;
	if ((match && (state->show_homepage || state->show_desc)) ||
			(!match && state->search_desc))
	{
		meta = tree_pkg_read(pkg_ctx);
		if (meta != NULL) {
			if (state->show_homepage)
				desc = meta->HOMEPAGE;
			else if (state->show_desc)
				desc = meta->DESCRIPTION;
		}
	}

	if (!match && state->search_desc && desc != NULL &&
			regexec(&state->search_expr, desc, 0, NULL, 0) == 0)
		match = true;

	if (match) {
		const char *qfmt;
		if (state->show_repo) {
			atom = tree_get_atom(pkg_ctx, 1);
			qfmt = "%[CATEGORY]%[PN]%[REPO]";
		} else {
			qfmt = "%[CATEGORY]%[PN]";
		}
		printf("%s%s%s\n",
				atom_format(qfmt, atom),
				(state->show_name ? "" : " "),
				(state->show_name ? "" : desc ? desc : ""));
	}

	if (meta != NULL)
		tree_close_meta(meta);

	if (last_atom != NULL)
		atom_implode(last_atom);
	last_atom = atom;
	pkg_ctx->atom = NULL;  /* we stole the atom, make sure it won't get freed */

	return EXIT_SUCCESS;
}

int qsearch_main(int argc, char **argv)
{
	int ret = EXIT_SUCCESS;
	const char *search_me = NULL;
	int i;
	const char *overlay;
	size_t n;
	struct qsearch_state state = {
		.show_homepage = false,
		.show_name = false,
		.show_desc = false,
		.show_repo = false,
		.search_desc = false,
		.search_name = false,
	};

	while ((i = GETOPT_LONG(QSEARCH, qsearch, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsearch)
		case 'a': search_me           = ".*";  break;
		case 's': state.search_name   = true;  break;
		case 'S': state.search_desc   = true;  break;
		case 'N': state.show_name     = true;  break;
		case 'H': state.show_homepage = true;  break;
		case 'R': state.show_repo     = true;  break;
		}
	}

	/* set defaults */
	if (!state.show_homepage)
		state.show_desc = true;
	if (!state.search_name && !state.search_desc)
		state.search_name = true;

	/* compile expression */
	if (search_me == NULL) {
		if (argc == optind)
			qsearch_usage(EXIT_FAILURE);
		search_me = argv[optind];
	}
	xregcomp(&state.search_expr, search_me, REG_EXTENDED | REG_ICASE);

	/* use sorted order here so the duplicate reduction works reliably */
	array_for_each(overlays, n, overlay) {
		tree_ctx *t = tree_open(portroot, overlay);
		if (t != NULL) {
			ret |= tree_foreach_pkg_sorted(t, qsearch_cb, &state);
			tree_close(t);
		}
	}

	return ret;
}
