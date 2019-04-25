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

/* Run printf on an atom!  The format field takes the form:
 *  %{keyword}: Always display the field that matches "keyword"
 *  %[keyword]: Only display the field when it's valid (or pverbose)
 * The possible "keywords" are:
 *  CATEGORY  P  PN  PV  PVR  PF  PR  SLOT
 *    - these are all the standard portage variables (so see ebuild(5))
 *  pfx - the version qualifier if set (e.g. > < = !)
 *  sfx - the version qualifier if set (e.g. *)
 */
static void
qatom_printf(const char *format, const depend_atom *atom, int pverbose)
{
	char bracket;
	const char *fmt, *p;

	if (!atom) {
		printf("(NULL:atom)");
		return;
	}

	p = format;
	while (*p != '\0') {
		fmt = strchr(p, '%');
		if (fmt == NULL) {
			printf("%s", p);
			return;
		} else if (fmt != p)
			printf("%.*s", (int)(fmt - p), p);

		bracket = fmt[1];
		if (bracket == '{' || bracket == '[') {
			fmt += 2;
			p = strchr(fmt, bracket == '{' ? '}' : ']');
			if (p) {
				size_t len = p - fmt;
				bool showit = (bracket == '{') || pverbose;
#define HN(X) (X ? X : "<unset>")
				if (!strncmp("CATEGORY", fmt, len)) {
					if (showit || atom->CATEGORY)
						printf("%s", HN(atom->CATEGORY));
				} else if (!strncmp("P", fmt, len)) {
					if (showit || atom->P)
						printf("%s", HN(atom->P));
				} else if (!strncmp("PN", fmt, len)) {
					if (showit || atom->PN)
						printf("%s", HN(atom->PN));
				} else if (!strncmp("PV", fmt, len)) {
					if (showit || atom->PV)
						printf("%s", HN(atom->PV));
				} else if (!strncmp("PVR", fmt, len)) {
					if (showit || atom->PVR)
						printf("%s", HN(atom->PVR));
				} else if (!strncmp("PF", fmt, len)) {
					printf("%s", atom->PN);
					if (atom->PV)
						printf("-%s", atom->PV);
					if (atom->PR_int)
						printf("-r%i", atom->PR_int);
				} else if (!strncmp("PR", fmt, len)) {
					if (showit || atom->PR_int)
						printf("r%i", atom->PR_int);
				} else if (!strncmp("SLOT", fmt, len)) {
					if (showit || atom->SLOT)
						printf("%s%s%s%s%s",
								atom->SLOT ? ":" : "<unset>",
								atom->SLOT ? atom->SLOT : "",
								atom->SUBSLOT ? "/" : "",
								atom->SUBSLOT ? atom->SUBSLOT : "",
								atom_slotdep_str[atom->slotdep]);
				} else if (!strncmp("REPO", fmt, len)) {
					if (showit || atom->REPO)
						printf("::%s", HN(atom->REPO));
				} else if (!strncmp("pfx", fmt, len)) {
					if (showit || atom->pfx_op != ATOM_OP_NONE)
						printf("%s", atom->pfx_op == ATOM_OP_NONE ?
								"<unset>" : atom_op_str[atom->pfx_op]);
				} else if (!strncmp("sfx", fmt, len)) {
					if (showit || atom->sfx_op != ATOM_OP_NONE)
						printf("%s", atom->sfx_op == ATOM_OP_NONE ?
								"<unset>" : atom_op_str[atom->sfx_op]);
				} else
					printf("<BAD:%.*s>", (int)len, fmt);
				++p;
#undef HN
			} else
				p = fmt + 1;
		} else
			++p;
	}
}

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
			qatom_printf(format, atom, verbose);
			putchar('\n');
			break;
		case _PRINT:
			printf("%s\n", atom_to_string(atom));
			break;
		}

		atom_implode(atom);
	}

	return EXIT_SUCCESS;
}
