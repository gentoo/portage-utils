/*
 * Copyright 2005-2019 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <ctype.h>
#include <xalloc.h>
#include <assert.h>

#include "atom.h"
#include "dep.h"
#include "set.h"
#include "vdb.h"
#include "xarray.h"
#include "xasprintf.h"
#include "xregex.h"

#define QDEPENDS_FLAGS "drpbfNQu" COMMON_FLAGS
static struct option const qdepends_long_opts[] = {
	{"depend",    no_argument, NULL, 'd'},
	{"rdepend",   no_argument, NULL, 'r'},
	{"pdepend",   no_argument, NULL, 'p'},
	{"bdepend",   no_argument, NULL, 'b'},
	{"query",     no_argument, NULL, 'Q'},
	{"name-only", no_argument, NULL, 'N'},
	{"format",    no_argument, NULL, 'f'},
	COMMON_LONG_OPTS
};
static const char * const qdepends_opts_help[] = {
	"Show DEPEND info",
	"Show RDEPEND info",
	"Show PDEPEND info",
	"Show BDEPEND info",
	"Query reverse deps",
	"Only show package name",
	"Pretty format specified depend strings",
	COMMON_OPTS_HELP
};
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, NULL, lookup_applet_idx("qdepends"))

static char qdep_name_only = 0;

/* structures / types / etc ... */
struct qdepends_opt_state {
	unsigned char qmode;
	array_t *atoms;
	array_t *deps;
	set *udeps;
	char *depend;
	size_t depend_len;
};

#define QMODE_DEPEND     (1<<0)
#define QMODE_RDEPEND    (1<<1)
#define QMODE_PDEPEND    (1<<2)
#define QMODE_BDEPEND    (1<<3)
#define QMODE_REVERSE    (1<<7)

const char *depend_files[] = {  /* keep *DEPEND aligned with above defines */
	/* 0 */ "DEPEND",
	/* 1 */ "RDEPEND",
	/* 2 */ "PDEPEND",
	/* 3 */ "BDEPEND",
	/* 4 */ NULL
};

static bool
qdepends_print_depend(FILE *fp, const char *depend)
{
	dep_node *dep_tree;

	dep_tree = dep_grow_tree(depend);
	if (dep_tree == NULL)
		return false;

	if (!quiet)
		fprintf(fp, "DEPEND=\"\n");

	dep_print_tree(fp, dep_tree, 1, NULL, NORM, verbose > 1);

	dep_burn_tree(dep_tree);
	if (!quiet)
		fprintf(fp, "\"\n");

	return true;
}

static int
qdepends_results_cb(q_vdb_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
	const char *catname = pkg_ctx->cat_ctx->name;
	const char *pkgname = pkg_ctx->name;
	depend_atom *atom;
	depend_atom *datom;
	depend_atom *fatom;
	bool firstmatch = false;
	char buf[_Q_PATH_MAX];
	const char **dfile;
	size_t i;
	size_t n;
	size_t m;
	int ret = 0;
	dep_node *dep_tree;
	char **d;

	/* matrix consists of:
	 * - QMODE_*DEPEND
	 * - QMODE_REVERSE or not
	 *
	 * REVERSE vs forward mode requires a different search strategy,
	 * *DEPEND alters the search somewhat and affects results printing.
	 */

	snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
	datom = atom_explode(buf);
	if (datom == NULL)
		return ret;

	if ((state->qmode & QMODE_REVERSE) == 0) {
		/* see if this cat/pkg is requested */
		array_for_each(state->atoms, i, atom) {
			if (atom_compare(atom, datom) == EQUAL) {
				atom = NULL;
				break;
			}
		}

		/* nothing matched */
		if (atom != NULL) {
			atom_implode(datom);
			return ret;
		}

		ret = 1;

		printf("%s%s/%s%s%s:", BOLD, catname, BLUE,
				qdep_name_only ? datom->PN : pkgname, NORM);
	}

	xarrayfree_int(state->deps);
	clear_set(state->udeps);

	dfile = depend_files;
	for (i = QMODE_DEPEND; i <= QMODE_BDEPEND; i <<= 1, dfile++) {
		if (!(state->qmode & i))
			continue;
		if (!q_vdb_pkg_eat(pkg_ctx, *dfile,
					&state->depend, &state->depend_len))
			continue;

		dep_tree = dep_grow_tree(state->depend);
		if (dep_tree == NULL)
			continue;

		dep_flatten_tree(dep_tree, state->deps);

		if (verbose) {
			if (state->qmode & QMODE_REVERSE) {
				array_for_each(state->atoms, m, atom) {
					array_for_each(state->deps, n, fatom) {
						if (atom_compare(atom, fatom) == EQUAL) {
							fatom = NULL;
							break;
						}
					}
					if (fatom == NULL) {
						atom = NULL;
						break;
					}
				}
				if (atom == NULL) {
					ret = 1;

					if (!firstmatch) {
						printf("%s%s/%s%s%s:",
								BOLD, catname, BLUE,
								qdep_name_only ? datom->PN : pkgname, NORM);
					}
					firstmatch = true;

					printf("\n%s=\"\n", *dfile);
					dep_print_tree(stdout, dep_tree, 1, state->atoms,
							RED, verbose > 1);
					printf("\"");
				}
			} else {
				printf("\n%s=\"\n", *dfile);
				dep_print_tree(stdout, dep_tree, 1, state->deps,
						GREEN, verbose > 1);
				printf("\"");
			}
		} else {
			if (state->qmode & QMODE_REVERSE) {
				array_for_each(state->deps, m, atom) {
					array_for_each(state->atoms, n, fatom) {
						if (atom_compare(atom, fatom) == EQUAL) {
							fatom = NULL;
							break;
						}
					}
					if (fatom == NULL) {
						ret = 1;

						if (!firstmatch) {
							printf("%s%s/%s%s%s%s",
									BOLD, catname, BLUE,
									qdep_name_only ? datom->PN : pkgname, NORM,
									quiet ? "" : ":");
						}
						firstmatch = true;

						snprintf(buf, sizeof(buf), "%s%s%s",
								RED, atom_to_string(atom), NORM);
						add_set_unique(buf, state->udeps, NULL);
					} else if (!quiet) {
						add_set_unique(atom_to_string(atom),
								state->udeps, NULL);
					}
				}
			} else {
				array_for_each(state->deps, m, atom)
					add_set_unique(atom_to_string(atom), state->udeps, NULL);
			}
		}

		xarrayfree_int(state->deps);
		dep_burn_tree(dep_tree);
	}
	if (verbose && ret == 1)
		printf("\n");

	if (!verbose) {
		if ((state->qmode & QMODE_REVERSE) == 0 || ret == 1) {
			for (n = list_set(state->udeps, &d); n > 0; n--)
				printf(" %s", d[n -1]);
			free(d);
			printf("\n");
		}
	}

	atom_implode(datom);

	return ret;
}

int qdepends_main(int argc, char **argv)
{
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	DECLARE_ARRAY(deps);
	struct qdepends_opt_state state = {
		.atoms = atoms,
		.deps = deps,
		.udeps = create_set(),
		.qmode = 0,
		.depend = NULL,
		.depend_len = 0,
	};
	size_t i;
	int ret;
	bool do_format = false;

	while ((ret = GETOPT_LONG(QDEPENDS, qdepends, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qdepends)

		case 'd': state.qmode |= QMODE_DEPEND;  break;
		case 'r': state.qmode |= QMODE_RDEPEND; break;
		case 'p': state.qmode |= QMODE_PDEPEND; break;
		case 'b': state.qmode |= QMODE_BDEPEND; break;
		case 'Q': state.qmode |= QMODE_REVERSE; break;
		case 'N': qdep_name_only = 1; break;
		case 'f': do_format = true; break;
		}
	}

	if ((state.qmode & ~QMODE_REVERSE) == 0) {
		/* default mode of operation: -qau (also for just -Q) */
		state.qmode |= QMODE_DEPEND  |
					   QMODE_RDEPEND |
					   QMODE_PDEPEND |
					   QMODE_BDEPEND;
	}

	if ((argc == optind) && !do_format)
		qdepends_usage(EXIT_FAILURE);

	if (do_format) {
		while (optind < argc) {
			if (!qdepends_print_depend(stdout, argv[optind++]))
				return EXIT_FAILURE;
			if (optind < argc)
				fprintf(stdout, "\n");
		}
		return EXIT_SUCCESS;
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

	ret = q_vdb_foreach_pkg(portroot, portvdb,
			qdepends_results_cb, &state, NULL);

	if (state.depend != NULL)
		free(state.depend);

	array_for_each(atoms, i, atom)
		atom_implode(atom);
	xarrayfree_int(atoms);
	xarrayfree_int(state.deps);
	free_set(state.udeps);

	if (!ret)
		warn("no matches found for your query");
	return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}
