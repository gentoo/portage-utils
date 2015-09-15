/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qatom

#define QATOM_FLAGS "c" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	{"compare",   no_argument, NULL, 'c'},
	COMMON_LONG_OPTS
};
static const char * const qatom_opts_help[] = {
	"Compare two atoms",
	COMMON_OPTS_HELP
};
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

	if (action == _COMPARE && (argc - optind) % 2)
		err("compare needs even number of arguments");

	for (i = optind; i < argc; ++i) {
		switch (action) {
		case _COMPARE:
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
			if (verbose || atom->SLOT)
				printf(" :%s", atom->SLOT ? atom->SLOT : "-");
			if (verbose || atom->pfx_op != ATOM_OP_NONE)
				printf(" %s", atom->pfx_op == ATOM_OP_NONE ? "-" : atom_op_str[atom->pfx_op]);
			if (verbose || atom->sfx_op != ATOM_OP_NONE)
				printf(" %s", atom->sfx_op == ATOM_OP_NONE ? "-" : atom_op_str[atom->sfx_op]);
			if (verbose > 1)
				printf(" %c", (atom->letter ? : '-'));
			putchar('\n');
			atom_implode(atom);
		}
	}

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qatom)
#endif
