/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"

#include <stdlib.h>
#include <stdbool.h>

#include "atom.h"
#include "applets.h"

#define QATOM_FORMAT "%{CATEGORY} %{PN} %{PV} %[PR] %[SLOT] %[pfx] %[sfx]"

#define QATOM_FLAGS "F:cp" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	{"format",     a_argument, NULL, 'F'},
	{"compare",   no_argument, NULL, 'c'},
	{"print",     no_argument, NULL, 'p'},
	COMMON_LONG_OPTS
};
static const char * const qatom_opts_help[] = {
	"Custom output format (default: " QATOM_FORMAT ")",
	"Compare two atoms",
	"Print reconstructed atom",
	COMMON_OPTS_HELP
};
#define qatom_usage(ret) usage(ret, QATOM_FLAGS, qatom_long_opts, qatom_opts_help, NULL, lookup_applet_idx("qatom"))

int qatom_main(int argc, char **argv)
{
	enum qatom_atom { _EXPLODE=0, _COMPARE, _PRINT } action = _EXPLODE;
	const char *format = QATOM_FORMAT;
	depend_atom *atom;
	depend_atom *atomc;
	int i;

	while ((i = GETOPT_LONG(QATOM, qatom, "")) != -1) {
		switch (i) {
		case 'F': format = optarg; break;
		case 'c': action = _COMPARE; break;
		case 'p': action = _PRINT; break;
		COMMON_GETOPTS_CASES(qatom)
		}
	}

	if (argc == optind)
		qatom_usage(EXIT_FAILURE);

	if (action == _COMPARE && (argc - optind) % 2)
		err("compare needs even number of arguments");

	for (i = optind; i < argc; ++i) {
		atom = atom_explode(argv[i]);
		if (atom == NULL) {
			warnf("invalid atom: %s\n", argv[i]);
			continue;
		}

		switch (action) {
		case _COMPARE:
			i++;
			atomc = atom_explode(argv[i]);
			if (atomc == NULL) {
				warnf("invalid atom: %s\n", argv[i]);
				break;
			}
			printf("%s %s ",
					atom_to_string(atom),
					booga[atom_compare(atom, atomc)]);
			printf("%s\n",
					atom_to_string(atomc));
			atom_implode(atomc);
			break;
		case _EXPLODE:
			printf("%s\n", atom_format(format, atom, verbose));
			break;
		case _PRINT:
			printf("%s\n", atom_to_string(atom));
			break;
		}

		atom_implode(atom);
	}

	return EXIT_SUCCESS;
}
