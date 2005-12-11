/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qatom.c,v 1.1 2005/12/11 18:58:13 solar Exp $
 *
 * Copyright 2005 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005 Mike Frysinger  - <vapier@gentoo.org>
 */

#define QATOM_FLAGS "" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qatom_opts_help[] = {
	COMMON_OPTS_HELP
};

static const char qatom_rcsid[] = "$Id: qatom.c,v 1.1 2005/12/11 18:58:13 solar Exp $";
#define qatom_usage(ret) usage(ret, QATOM_FLAGS, qatom_long_opts, qatom_opts_help, lookup_applet_idx("qatom"))

int qatom_main(int argc, char **argv)
{
	depend_atom *atom;
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	if (argc == 1)
		qatom_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(QATOM, qatom, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qatom)
		}
	}

	for (i = optind; i < argc; ++i) {
		atom = atom_explode(argv[i]);
		if (!atom) {
			warnf("failed exploding atom %s", argv[i]);
			continue;
		}
		printf("%s %s %s", atom->CATEGORY, atom->PN, atom->PV);
		if (verbose || atom->PR_int)
			printf(" r%i", atom->PR_int);
		putchar('\n');
		atom_implode(atom);
	}

	return EXIT_SUCCESS;
}
