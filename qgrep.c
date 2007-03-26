/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qgrep.c,v 1.18 2007/03/26 16:17:34 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Petteri Räty    - <betelgeuse@gentoo.org>
 */

#ifdef APPLET_qgrep

#define QGREP_FLAGS "IiHNclLexEsS:" COMMON_FLAGS
static struct option const qgrep_long_opts[] = {
	{"invert-match",  no_argument, NULL, 'I'},
	{"ignore-case",   no_argument, NULL, 'i'},
	{"with-filename", no_argument, NULL, 'H'},
	{"with-name",     no_argument, NULL, 'N'},
	{"count",         no_argument, NULL, 'c'},
	{"list",          no_argument, NULL, 'l'},
	{"invert-list",   no_argument, NULL, 'L'},
	{"regexp",        no_argument, NULL, 'e'},
	{"extended",      no_argument, NULL, 'x'},
	{"eclass",        no_argument, NULL, 'E'},
	{"skip-comments", no_argument, NULL, 's'},
	{"skip",           a_argument, NULL, 'S'},
	COMMON_LONG_OPTS
};
static const char *qgrep_opts_help[] = {
	"Select non-matching lines",
	"Ignore case distinctions",
	"Print the filename for each match",
	"Print the package or eclass name for each match",
	"Only print a count of matching lines per FILE",
	"Only print FILE names containing matches",
	"Only print FILE names containing no match",
	"Use PATTERN as a regular expression",
	"Use PATTERN as an extended regular expression",
	"Search in eclasses instead of ebuilds",
	"Skip comments lines",
	"Skip lines matching <arg>",
	COMMON_OPTS_HELP
};
static const char qgrep_rcsid[] = "$Id: qgrep.c,v 1.18 2007/03/26 16:17:34 solar Exp $";
#define qgrep_usage(ret) usage(ret, QGREP_FLAGS, qgrep_long_opts, qgrep_opts_help, lookup_applet_idx("qgrep"))

char qgrep_name_match(const char*, const int, depend_atom**);
char qgrep_name_match(const char* name, const int argc, depend_atom** argv)
{
	depend_atom* atom;
	int i;

	if ((atom = atom_explode(name)) == NULL)
		return 0;

	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL)
			continue;
		if (atom->CATEGORY && argv[i]->CATEGORY && *(argv[i]->CATEGORY)
				&& strcmp(atom->CATEGORY, argv[i]->CATEGORY))
			continue;
		if (atom->PN && argv[i]->PN && *(argv[i]->PN)
				&& strcmp(atom->PN, argv[i]->PN))
			continue;
		if (atom->PVR && argv[i]->PVR && *(argv[i]->PVR)
				&& strcmp(atom->PVR, argv[i]->PVR))
			continue;
		atom_implode(atom);
		return 1;
	}

	atom_implode(atom);
	return 0;
}

int qgrep_main(int argc, char **argv)
{
	int i;
	int count = 0;
	char *p;
	char do_count, do_regex, do_eclass, do_list;
	char show_filename, skip_comments, invert_list, show_name;
	char per_file_output;
	FILE *fp = NULL;
	DIR *eclass_dir = NULL;
	struct dirent *dentry;
	char ebuild[_Q_PATH_MAX];
	char name[_Q_PATH_MAX];
	char buf0[BUFSIZ];
	int reflags = REG_NOSUB;
	char invert_match = 0;
	regex_t preg, skip_preg;
	char *skip_pattern = NULL;
	depend_atom** include_atoms = NULL;

	typedef char *(*FUNC) (char *, char *);
	FUNC strfunc = (FUNC) strstr;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	do_count = do_regex = do_eclass = do_list = 0;
	show_filename = skip_comments = invert_list = show_name = 0;

	while ((i = GETOPT_LONG(QGREP, qgrep, "")) != -1) {
		switch (i) {
		case 'I': invert_match = 1; break;
		case 'i':
			strfunc = (FUNC) strcasestr;
			reflags |= REG_ICASE;
			break;
		case 'c': do_count = 1; break;
		case 'l': do_list = 1; break;
		case 'L': do_list = invert_list = 1; break;
		case 'e': do_regex = 1; break;
		case 'x':
			do_regex = 1;
			reflags |= REG_EXTENDED;
			break;
		case 'E': do_eclass = 1; break;
		case 'H': show_filename = 1; break;
		case 'N': show_name = 1; break;
		case 's': skip_comments = 1; break;
		case 'S': skip_pattern = optarg; break;
		COMMON_GETOPTS_CASES(qgrep)
		}
	}
	if (argc == optind)
		qgrep_usage(EXIT_FAILURE);

	if (do_list && do_count) {
		warn("%s and --count are incompatible options. The former wins.",
				(invert_list ? "--invert-list" : "--list"));
		do_count = 0;
	}

	if (show_name && show_filename) {
		warn("--with-name and --with-filename are incompatible options. The former wins.");
		show_filename = 0;
	}

	/* do we report results once per file or per line ? */
	per_file_output = do_count || (do_list && (!verbose || invert_list));

	if (argc > (optind + 1)) {
		include_atoms = xcalloc(sizeof(depend_atom*), (argc - optind - 1));
		for (i = (optind + 1); i < argc; i++)
			if ((include_atoms[i - optind - 1] = atom_explode(argv[i])) == NULL)
				warn("%s: invalid atom, will be ignored", argv[i]);
	}

	if (do_regex) {
		int ret;
		char err[256];
		if ((ret = regcomp(&preg, argv[optind], reflags))) {
			if (regerror(ret, &preg, err, sizeof(err)))
				err("regcomp failed: %s", err);
			else
				err("regcomp failed");
		}
		if (skip_pattern && (ret = regcomp(&skip_preg, skip_pattern, reflags))) {
			if (regerror(ret, &skip_preg, err, sizeof(err)))
				err("regcomp failed for --skip pattern: %s", err);
			else
				err("regcomp failed for --skip pattern");
		}
	}

	if (!do_eclass) {
		initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
		if ((fp = fopen(CACHE_EBUILD_FILE, "r")) == NULL)
			return 1;
	} else {
		if ((chdir(portdir)) != 0)
			errp("chdir to PORTDIR '%s' failed", portdir);
		if ((eclass_dir = opendir("eclass")) == NULL)
			errp("opendir(\"%s/eclass\") failed", portdir);
	}

	while (do_eclass
			? ((dentry = readdir(eclass_dir))
				&& snprintf(ebuild, sizeof(ebuild), "eclass/%s", dentry->d_name))
			: ((fgets(ebuild, sizeof(ebuild), fp)) != NULL)) {
		FILE *newfp;
		if (do_eclass) {
			if ((p = strrchr(ebuild, '.')) == NULL)
				continue;
			if (strcmp(p, ".eclass"))
				continue;
			if (show_name || (include_atoms != NULL)) {
				/* cut ".eclass" */
				*p = '\0';
				/* and skip "eclass/" */
				snprintf(name, sizeof(name), "%s", ebuild + 7);
				/* restore the filepath */
				*p = '.';
			}
		} else {
			if ((p = strchr(ebuild, '\n')) != NULL)
				*p = '\0';
			if (show_name || (include_atoms != NULL)) {
				/* cut ".ebuild" */
				if (p == NULL)
					p = ebuild + strlen(ebuild);
				*(p-7) = '\0';
				/* cut "/foo/" from "cat/foo/foo-x.y" */
				if ((p = strchr(ebuild, '/')) == NULL)
					continue;
				*(p++) = '\0';
				/* find head of the ebuild basename */
				if ((p = strchr(p, '/')) == NULL)
					continue;
				/* find	start of the pkg name */
				snprintf(name, sizeof(name), "%s/%s", ebuild, (p+1));
				/* restore the filepath */
				*p = '/';
				*(p + strlen(p)) = '.';
				ebuild[strlen(ebuild)] = '/';
			}
		}

		if (include_atoms != NULL)
			if (!qgrep_name_match(name, (argc - optind - 1), include_atoms))
				continue;

		if ((newfp = fopen(ebuild, "r")) != NULL) {
			unsigned int lineno = 0;
			count = 0;

			while ((fgets(buf0, sizeof(buf0), newfp)) != NULL) {
				lineno++;
				if ((p = strrchr(buf0, '\n')) != NULL)
					*p = 0;
				if ((p = strrchr(buf0, '\r')) != NULL)
					*p = 0;

				if (skip_comments) {
					p = buf0;
					while (*p == ' ' || *p == '\t') p++;
					if (*p == '#') continue;
				}

				if (skip_pattern) {
					if (!do_regex) {
						if (( (FUNC *) (strfunc) (buf0, skip_pattern)) != NULL) continue;
					} else {
						if (regexec(&skip_preg, buf0, 0, NULL, 0) == 0) continue;
					}
				}

				if (!invert_match) {
					if (do_regex == 0) {
						if (( (FUNC *) (strfunc) (buf0, argv[optind])) == NULL) continue;
					} else {
						if (regexec(&preg, buf0, 0, NULL, 0) != 0) continue;
					}
				} else {
					if (do_regex == 0) {
						if (( (FUNC *) (strfunc) (buf0, argv[optind])) != NULL) continue;
					} else {
						if (regexec(&preg, buf0, 0, NULL, 0) == 0) continue;
					}
				}

				count++;
				if (per_file_output) continue;
				if (verbose || show_filename || show_name) {
					printf("%s", (show_name ? name : ebuild));
					if (verbose > 1) printf(":%d", lineno);
					if (!do_list)
						printf(": ");
				}
				printf("%s\n",  (do_list ? "" : buf0));
			}
			fclose(newfp);
			if (!per_file_output) continue;
			if (do_count && count) {
				if (verbose || show_filename || show_name)
					printf("%s:", (show_name ? name : ebuild));
				printf("%d", count);
				puts("");
			} else if (do_list && ((count && !invert_list) || (!count && invert_list)))
				printf("%s\n", (show_name ? name : ebuild));
		}
	}
	if (do_eclass)
		closedir(eclass_dir);
	else
		fclose(fp);
	if (do_regex)
		regfree(&preg);
	if (do_regex && skip_pattern)
		regfree(&skip_preg);
	if (include_atoms != NULL) {
		for (i = 0; i < (argc - optind - 1); i++)
			if (include_atoms[i] != NULL)
				atom_implode(include_atoms[i]);
		free(include_atoms);
	}
	return EXIT_SUCCESS;
}

#else
DEFINE_APPLET_STUB(qgrep)
#endif
