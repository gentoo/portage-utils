/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include "atom.h"
#include "tree.h"

#define QATOM_FORMAT "%{CATEGORY} %{PN} %{PV} %[PR] %[SLOT] %[pfx] %[sfx]"

#define QATOM_FLAGS "F:cpl" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	{"format",     a_argument, NULL, 'F'},
	{"compare",   no_argument, NULL, 'c'},
	{"print",     no_argument, NULL, 'p'},
	{"lookup",    no_argument, NULL, 'l'},
	COMMON_LONG_OPTS
};
static const char * const qatom_opts_help[] = {
	"Custom output format (default: " QATOM_FORMAT ")",
	"Compare two atoms",
	"Print reconstructed atom",
	"Lookup atom in tree",
	COMMON_OPTS_HELP
};
#define qatom_usage(ret) usage(ret, QATOM_FLAGS, qatom_long_opts, qatom_opts_help, NULL, lookup_applet_idx("qatom"))

int qatom_main(int argc, char **argv)
{
	enum qatom_atom { _EXPLODE=0, _COMPARE, _PRINT, _LOOKUP } action = _EXPLODE;
	const char *format = QATOM_FORMAT;
	depend_atom *atom;
	depend_atom *atomc;
	tree_ctx *tree = NULL;
	int i;

	while ((i = GETOPT_LONG(QATOM, qatom, "")) != -1) {
		switch (i) {
		case 'F': format = optarg;   break;
		case 'c': action = _COMPARE; break;
		case 'p': action = _PRINT;   break;
		case 'l': action = _LOOKUP;  break;
		COMMON_GETOPTS_CASES(qatom)
		}
	}

	if (argc == optind)
		qatom_usage(EXIT_FAILURE);

	if (action == _COMPARE && (argc - optind) % 2)
		err("compare needs even number of arguments");

	if (action == _LOOKUP) {
		tree = tree_open(portroot, main_overlay);
		if (tree == NULL)
			err("failed to open tree");
	}

	for (i = optind; i < argc; i++) {
		atom = atom_explode(argv[i]);
		if (atom == NULL) {
			warnf("invalid atom: %s\n", argv[i]);
			continue;
		}

		switch (action) {
		case _COMPARE: {
			int r;

			i++;
			atomc = atom_explode(argv[i]);
			if (atomc == NULL) {
				warnf("invalid atom: %s\n", argv[i]);
				break;
			}

			if (atomc->blocker != ATOM_BL_NONE ||
					atomc->pfx_op != ATOM_OP_NONE ||
					atomc->sfx_op != ATOM_OP_NONE ||
					(atomc->CATEGORY == NULL &&
					 atom->blocker == ATOM_BL_NONE &&
					 atom->pfx_op == ATOM_OP_NONE &&
					 atom->sfx_op == ATOM_OP_NONE))
			{
				r = atom_compare(atom, atomc);
			} else {
				r = atom_compare(atomc, atom);
				switch (r) {
					case NEWER:     r = OLDER;     break;
					case OLDER:     r = NEWER;     break;
				}
			}

			printf("%s %s ", atom_to_string(atom), booga[r]);
			printf("%s\n", atom_to_string(atomc));
			atom_implode(atomc);
			break;
		}
		case _EXPLODE:
			printf("%s\n", atom_format(format, atom));
			break;
		case _PRINT:
			printf("%s\n", atom_to_string(atom));
			break;
		case _LOOKUP:
			{
				tree_pkg_ctx *pkg = tree_match_atom(tree, atom);
				if (pkg != NULL) {
					atomc = tree_get_atom(pkg, true);
					if (!quiet)
						printf("%s: ", atom_to_string(atom));
					printf("%s\n", atom_format(format, atomc));
				}
			}
		}

		atom_implode(atom);
	}

	if (action == _LOOKUP)
		tree_close(tree);

	return EXIT_SUCCESS;
}
