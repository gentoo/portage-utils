/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qatom.c,v 1.3 2006/01/24 23:35:08 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef OMIT_QATOM

#define QATOM_FLAGS "c" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	{"compare",   no_argument, NULL, 'c'},
	COMMON_LONG_OPTS
};
static const char *qatom_opts_help[] = {
	"Compare two atoms",
	COMMON_OPTS_HELP
};

static const char qatom_rcsid[] = "$Id: qatom.c,v 1.3 2006/01/24 23:35:08 vapier Exp $";
#define qatom_usage(ret) usage(ret, QATOM_FLAGS, qatom_long_opts, qatom_opts_help, lookup_applet_idx("qatom"))

int qatom_main(int argc, char **argv)
{
	enum qatom_atom { _EXPLODE=0, _COMPARE } action = _EXPLODE;
	depend_atom *atom;
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QATOM, qatom, "")) != -1) {
		switch (i) {
		case 'c': action = _COMPARE; break;
		COMMON_GETOPTS_CASES(qatom)
		}
	}

	if (argc == optind)
		qatom_usage(EXIT_FAILURE);

	for (i = optind; i < argc; ++i) {
		switch (action) {
		case _COMPARE:
			if (i+1 == argc)
				errf("Wrong number of arguments");
			printf("%s %s %s\n", argv[i], booga[atom_compare_str(argv[i], argv[i+1])], argv[i+1]);
			++i;
			break;
		case _EXPLODE:
			atom = atom_explode(argv[i]);
			if (!atom) {
				warnf("failed exploding atom %s", argv[i]);
				continue;
			}
			printf("%s %s %s", atom->CATEGORY, atom->PN, atom->PV);
			if (verbose || atom->PR_int)
				printf(" r%i", atom->PR_int);
			if (verbose > 1)
				printf(" %c", (atom->letter ? : '-'));
			putchar('\n');
			atom_implode(atom);
		}
	}

	return EXIT_SUCCESS;
}

#endif
