/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qatom

#define QATOM_FORMAT "%{CATEGORY} %{PN} %{PV} %[PR] %[SLOT] %[pfx] %[sfx]"

#define QATOM_FLAGS "F:c" COMMON_FLAGS
static struct option const qatom_long_opts[] = {
	{"format",     a_argument, NULL, 'F'},
	{"compare",   no_argument, NULL, 'c'},
	COMMON_LONG_OPTS
};
static const char * const qatom_opts_help[] = {
	"Custom output format (default: " QATOM_FORMAT ")",
	"Compare two atoms",
	COMMON_OPTS_HELP
};
#define qatom_usage(ret) usage(ret, QATOM_FLAGS, qatom_long_opts, qatom_opts_help, lookup_applet_idx("qatom"))

/* Run printf on an atom!  The format field takes the form:
 *  %{keyword}: Always display the field that matches "keyword"
 *  %[keyword]: Only display the field when it's valid (or pverbose)
 * The possible "keywords" are:
 *  CATEGORY  P  PN  PV  PVR  PF  PR  SLOT
 *    - these are all the standard portage variables (so see ebuild(5))
 *  pfx - the version qualifier if set (e.g. > < = !)
 *  sfx - the version qualifier if set (e.g. *)
 */
_q_static
void qatom_printf(const char *format, const depend_atom *atom, int pverbose)
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
				if (!strncmp("CATEGORY", fmt, len)) {
					if (showit || atom->CATEGORY)
						printf("%s", atom->CATEGORY);
				} else if (!strncmp("P", fmt, len)) {
					if (showit || atom->P)
						printf("%s", atom->P);
				} else if (!strncmp("PN", fmt, len)) {
					if (showit || atom->PN)
						printf("%s", atom->PN);
				} else if (!strncmp("PV", fmt, len)) {
					if (showit || atom->PV)
						printf("%s", atom->PV);
				} else if (!strncmp("PVR", fmt, len)) {
					if (showit || atom->PVR)
						printf("%s", atom->PVR);
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
						printf(":%s", atom->SLOT ? : "-");
				} else if (!strncmp("pfx", fmt, len)) {
					if (showit || atom->pfx_op != ATOM_OP_NONE)
						fputs(atom->pfx_op == ATOM_OP_NONE ? "-" : atom_op_str[atom->pfx_op], stdout);
				} else if (!strncmp("sfx", fmt, len)) {
					if (showit || atom->sfx_op != ATOM_OP_NONE)
						fputs(atom->sfx_op == ATOM_OP_NONE ? "-" : atom_op_str[atom->sfx_op], stdout);
				} else
					printf("<BAD:%.*s>", (int)len, fmt);
				++p;
			} else
				p = fmt + 1;
		} else
			++p;
	}
}

int qatom_main(int argc, char **argv)
{
	enum qatom_atom { _EXPLODE=0, _COMPARE } action = _EXPLODE;
	const char *format = QATOM_FORMAT;
	depend_atom *atom;
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QATOM, qatom, "")) != -1) {
		switch (i) {
		case 'F': format = optarg; break;
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
			qatom_printf(format, atom, verbose);
			putchar('\n');
			atom_implode(atom);
			break;
		}
	}

	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qatom)
#endif
