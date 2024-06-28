/*
 * Copyright 2005-2023 Gentoo Authors
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
#include "tree.h"
#include "xarray.h"
#include "xasprintf.h"
#include "xregex.h"

#define QDEPENDS_FLAGS "drpbIQitUF:SR" COMMON_FLAGS
static struct option const qdepends_long_opts[] = {
	{"depend",    no_argument, NULL, 'd'},
	{"rdepend",   no_argument, NULL, 'r'},
	{"pdepend",   no_argument, NULL, 'p'},
	{"bdepend",   no_argument, NULL, 'b'},
	{"idepend",   no_argument, NULL, 'I'},
	{"query",     no_argument, NULL, 'Q'},
	{"installed", no_argument, NULL, 'i'},
	{"tree",      no_argument, NULL, 't'},
	{"use",       no_argument, NULL, 'U'},
	{"format",     a_argument, NULL, 'F'},
	{"pretty",    no_argument, NULL, 'S'},
	{"resolve",   no_argument, NULL, 'R'},
	COMMON_LONG_OPTS
};
static const char * const qdepends_opts_help[] = {
	"Show DEPEND info",
	"Show RDEPEND info",
	"Show PDEPEND info",
	"Show BDEPEND info",
	"Show IDEPEND info",
	"Query reverse deps",
	"Search installed packages using VDB",
	"Search available ebuilds in the tree",
	"Apply profile USE-flags to conditional deps",
	"Print matched atom using given format string",
	"Pretty format specified depend strings",
	"Resolve found dependencies to package versions",
	COMMON_OPTS_HELP
};
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, NULL, lookup_applet_idx("qdepends"))

/* structures / types / etc ... */
struct qdepends_opt_state {
	unsigned int  qmode;
	array_t      *atoms;
	array_t      *deps;
	set          *udeps;
	char         *depend;
	size_t        depend_len;
	const char   *format;
	char          resolve:1;
	tree_ctx     *vdb;
	tree_ctx     *rtree;
};

#define QMODE_DEPEND     (1<<0)
#define QMODE_RDEPEND    (1<<1)
#define QMODE_PDEPEND    (1<<2)
#define QMODE_BDEPEND    (1<<3)
#define QMODE_IDEPEND    (1<<4)
#define QMODE_INSTALLED  (1<<5)
#define QMODE_TREE       (1<<6)
#define QMODE_REVERSE    (1<<7)
#define QMODE_FILTERUSE  (1<<8)

#define QMODE_DEP_FIRST  QMODE_DEPEND
#define QMODE_DEP_LAST   QMODE_IDEPEND

const char *depend_files[] = {  /* keep *DEPEND aligned with above defines */
	/* 0 */ "DEPEND",
	/* 1 */ "RDEPEND",
	/* 2 */ "PDEPEND",
	/* 3 */ "BDEPEND",
	/* 4 */ "IDEPEND",
	/* 5 */ NULL
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
qdepends_results_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qdepends_opt_state *state = priv;
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
	char *depstr;

	/* matrix consists of:
	 * - QMODE_*DEPEND
	 * - QMODE_REVERSE or not
	 *
	 * REVERSE vs forward mode requires a different search strategy,
	 * *DEPEND alters the search somewhat and affects results printing.
	 */

	datom = tree_get_atom(pkg_ctx, false);
	if (datom == NULL)
		return ret;

	if ((state->qmode & QMODE_REVERSE) == 0) {
		/* see if this cat/pkg is requested */
		array_for_each(state->atoms, i, atom) {
			if (atom->blocker != ATOM_BL_NONE ||
					atom->SLOT != NULL ||
					atom->REPO != NULL)
				datom = tree_get_atom(pkg_ctx, true);
			if (atom_compare(datom, atom) == EQUAL) {
				atom = NULL;
				break;
			}
		}

		/* nothing matched */
		if (atom != NULL)
			return ret;

		ret = 1;

		datom = tree_get_atom(pkg_ctx, true);
		printf("%s:", atom_format(state->format, datom));
	}

	xarrayfree_int(state->deps);
	clear_set(state->udeps);

#define get_depstr(X) \
	i == QMODE_DEPEND  ? tree_pkg_meta_get(pkg_ctx, DEPEND)  : \
	i == QMODE_RDEPEND ? tree_pkg_meta_get(pkg_ctx, RDEPEND) : \
	i == QMODE_PDEPEND ? tree_pkg_meta_get(pkg_ctx, PDEPEND) : \
	i == QMODE_BDEPEND ? tree_pkg_meta_get(pkg_ctx, BDEPEND) : \
		                 tree_pkg_meta_get(pkg_ctx, IDEPEND) ;

	dfile = depend_files;
	for (i = QMODE_DEP_FIRST; i <= QMODE_DEP_LAST; i <<= 1, dfile++) {
		if (!(state->qmode & i))
			continue;

		depstr = get_depstr(i);
		if (depstr == NULL)
			continue;
		dep_tree = dep_grow_tree(depstr);
		if (dep_tree == NULL) {
			warn("failed to parse depstring from %s\n", atom_to_string(datom));
			continue;
		}

		/* try and resolve expressions to real package atoms */
		if (state->resolve)
			dep_resolve_tree(dep_tree, state->rtree);

		if (state->qmode & QMODE_TREE &&
			!(state->qmode & QMODE_REVERSE) &&
			verbose)
		{
			/* pull in flags in use if possible */
			tree_cat_ctx *vcat =
				tree_open_cat(state->vdb, pkg_ctx->cat_ctx->name);
			if (vcat != NULL) {
				tree_pkg_ctx *vpkg =
					tree_open_pkg(vcat, pkg_ctx->name);
				if (vpkg != NULL) {
					depstr = get_depstr(i);
					if (depstr != NULL) {
						dep_node *dep_vdb = dep_grow_tree(depstr);
						if (dep_vdb != NULL) {
							dep_flatten_tree(dep_vdb, state->deps);
							dep_burn_tree(dep_vdb);
						} else {
							warn("failed to parse VDB depstring from %s\n",
								 atom_to_string(datom));
						}
					}
					tree_close_pkg(vpkg);
				}
				tree_close_cat(vcat);
			}
		} else {
			if (state->qmode & QMODE_FILTERUSE)
				dep_prune_use(dep_tree, ev_use);
			dep_flatten_tree(dep_tree, state->deps);
		}

		if (verbose) {
			if (state->qmode & QMODE_REVERSE) {
				array_for_each(state->deps, m, atom) {
					array_for_each(state->atoms, n, fatom) {
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
						datom = tree_get_atom(pkg_ctx, true);
						printf("%s:", atom_format(state->format, datom));
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
							datom = tree_get_atom(pkg_ctx, true);
							printf("%s%s", atom_format(state->format, datom),
									quiet < 2 ? ":" : "");
						}
						firstmatch = true;

						snprintf(buf, sizeof(buf), "%s%s%s",
								RED, atom_to_string(atom), NORM);
						if (quiet < 2)
							state->udeps = add_set_unique(buf,
														  state->udeps, NULL);
					} else if (!quiet) {
						state->udeps = add_set_unique(atom_to_string(atom),
													  state->udeps, NULL);
					}
				}
			} else {
				array_for_each(state->deps, m, atom)
					state->udeps = add_set_unique(atom_to_string(atom),
												  state->udeps, NULL);
			}
		}

		xarrayfree_int(state->deps);
		dep_burn_tree(dep_tree);
	}
	if (verbose && ret == 1)
		printf("\n");

#undef get_depstr

	if (!verbose) {
		if ((state->qmode & QMODE_REVERSE) == 0 || ret == 1) {
			for (n = list_set(state->udeps, &d); n > 0; n--)
				printf(" %s", d[n -1]);
			free(d);
			printf("\n");
		}
	}

	return ret;
}

int qdepends_main(int argc, char **argv)
{
	depend_atom *atom;
	DECLARE_ARRAY(atoms);
	DECLARE_ARRAY(deps);
	struct qdepends_opt_state state = {
		.atoms   = atoms,
		.deps    = deps,
		.udeps   = create_set(),
		.qmode   = 0,
		.format  = "%[CATEGORY]%[PF]",
		.resolve = false,
		.vdb     = NULL,
		.rtree   = NULL,
	};
	size_t i;
	int ret;
	bool do_pretty = false;

	if (quiet)
		state.format = "%[CATEGORY]%[PN]";

	while ((ret = GETOPT_LONG(QDEPENDS, qdepends, "")) != -1) {
		switch (ret) {
		COMMON_GETOPTS_CASES(qdepends)

		case 'd': state.qmode |= QMODE_DEPEND;    break;
		case 'r': state.qmode |= QMODE_RDEPEND;   break;
		case 'p': state.qmode |= QMODE_PDEPEND;   break;
		case 'b': state.qmode |= QMODE_BDEPEND;   break;
		case 'I': state.qmode |= QMODE_IDEPEND;   break;
		case 'Q': state.qmode |= QMODE_REVERSE;   break;
		case 'i': state.qmode |= QMODE_INSTALLED; break;
		case 't': state.qmode |= QMODE_TREE;      break;
		case 'U': state.qmode |= QMODE_FILTERUSE; break;
		case 'S': do_pretty = true;               break;
		case 'R': state.resolve = true;           break;
		case 'F': state.format = optarg;          break;
		}
	}

	if ((state.qmode & ~(QMODE_REVERSE | QMODE_INSTALLED | QMODE_TREE)) == 0) {
		/* default mode of operation: -drpb (also for just -Q) */
		state.qmode |= QMODE_DEPEND  |
					   QMODE_RDEPEND |
					   QMODE_PDEPEND |
					   QMODE_BDEPEND |
					   QMODE_IDEPEND ;
	}

	/* default to installed packages */
	if (!(state.qmode & QMODE_INSTALLED) && !(state.qmode & QMODE_TREE))
		state.qmode |= QMODE_INSTALLED;

	/* don't allow both installed and from tree */
	if (state.qmode & QMODE_INSTALLED && state.qmode & QMODE_TREE) {
		warn("-i and -t cannot be used together, dropping -i");
		state.qmode &= ~QMODE_INSTALLED;
	}

	if ((argc == optind) && !do_pretty) {
		free_set(state.udeps);
		qdepends_usage(EXIT_FAILURE);
	}

	if (do_pretty) {
		ret = EXIT_SUCCESS;
		while (optind < argc) {
			if (!qdepends_print_depend(stdout, argv[optind++])) {
				ret = EXIT_FAILURE;
				break;
			}
			if (optind < argc)
				fprintf(stdout, "\n");
		}
		free_set(state.udeps);
		return ret;
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

	if (state.qmode & QMODE_INSTALLED || verbose)
		state.vdb = tree_open_vdb(portroot, portvdb);
	ret = 0;
	if (state.qmode & QMODE_TREE) {
		char *overlay;
		size_t n;
		tree_ctx *t;

		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL) {
				state.rtree = NULL;
				if (state.resolve)
					state.rtree = tree_open(portroot, overlay);
				if (!(state.qmode & QMODE_REVERSE) && array_cnt(atoms) > 0) {
					array_for_each(atoms, i, atom) {
						ret |= tree_foreach_pkg_sorted(t,
								qdepends_results_cb, &state, atom);
					}
				} else {
					ret |= tree_foreach_pkg_sorted(t,
							qdepends_results_cb, &state, NULL);
				}
				tree_close(t);
				if (state.rtree)
					tree_close(state.rtree);
			}
		}
	} else {  /* INSTALLED */
		if (state.resolve)
			state.rtree = tree_open_vdb(portroot, portvdb);
		if (!(state.qmode & QMODE_REVERSE) && array_cnt(atoms) > 0) {
			array_for_each(atoms, i, atom) {
				ret |= tree_foreach_pkg_fast(state.vdb,
						qdepends_results_cb, &state, atom);
			}
		} else {
			ret |= tree_foreach_pkg_fast(state.vdb,
					qdepends_results_cb, &state, NULL);
		}
		if (state.rtree)
			tree_close(state.rtree);
	}

	if (state.vdb != NULL)
		tree_close(state.vdb);
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
