/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2005 Petteri Räty    - <betelgeuse@gentoo.org>
 */

#ifdef APPLET_qgrep

#define QGREP_FLAGS "IiHNclLexJEsS:B:A:" COMMON_FLAGS
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
	{"installed",     no_argument, NULL, 'J'},
	{"eclass",        no_argument, NULL, 'E'},
	{"skip-comments", no_argument, NULL, 's'},
	{"skip",           a_argument, NULL, 'S'},
	{"before",         a_argument, NULL, 'B'},
	{"after",          a_argument, NULL, 'A'},
	COMMON_LONG_OPTS
};
static const char * const qgrep_opts_help[] = {
	"Select non-matching lines",
	"Ignore case distinctions",
	"Print the filename for each match",
	"Print the package or eclass name for each match",
	"Only print a count of matching lines per FILE",
	"Only print FILE names containing matches",
	"Only print FILE names containing no match",
	"Use PATTERN as a regular expression",
	"Use PATTERN as an extended regular expression",
	"Search in installed ebuilds instead of the tree",
	"Search in eclasses instead of ebuilds",
	"Skip comments lines",
	"Skip lines matching <arg>",
	"Print <arg> lines of leading context",
	"Print <arg> lines of trailing context",
	COMMON_OPTS_HELP
};
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

/* Circular list of line buffers for --before */
typedef struct qgrep_buf {
	char valid;
	/* 1 when the line should be included in
	 * a leading context, and 0 when it has already
	 * been displayed, or is from a previous file. */
	char buf[BUFSIZ];
	struct qgrep_buf *next;
} qgrep_buf_t;

/* Allocate <length> buffers in a circular list.
 * <length> must be at least 1. */
qgrep_buf_t* qgrep_buf_list_alloc(const char);
qgrep_buf_t* qgrep_buf_list_alloc(const char length)
{
	char i;
	qgrep_buf_t *head, *current;
	current = head = xmalloc(sizeof(qgrep_buf_t));
	for (i = 1; i < length; i++) {
		current->next = xmalloc(sizeof(qgrep_buf_t));
		current = current->next;
	}
	current->next = head;
	return head;
}

/* Free a circular buffers list. */
void qgrep_buf_list_free(qgrep_buf_t *);
void qgrep_buf_list_free(qgrep_buf_t *head)
{
	qgrep_buf_t *current, *next;
	next = head;
	do {
		current = next;
		next = current->next;
		free(current);
	} while (next != head);
}

/* Set valid=0 in the whole list. */
void qgrep_buf_list_invalidate(qgrep_buf_t *);
void qgrep_buf_list_invalidate(qgrep_buf_t *head)
{
	qgrep_buf_t *current;
	current = head;
	do {
		current->valid = 0;
		current = current->next;
	} while (current != head);
}

/* Type for the str(case)str search functions */
typedef char *(*QGREP_STR_FUNC) (const char *, const char *);

/* Display a buffer, with an optionnal prefix. */
void qgrep_print_line(qgrep_buf_t *, const char *, const int, const char,
		const regex_t*, const QGREP_STR_FUNC, const char*);
void qgrep_print_line(qgrep_buf_t *current, const char *label,
		const int line_number, const char zig, const regex_t* preg,
		const QGREP_STR_FUNC searchfunc, const char* searchstr)
{
	char *p = current->buf;
	/* Print line prefix, when in verbose mode */
	if (label != NULL) {
		printf("%s", label);
		if (line_number > 0)
			printf(":%d", line_number);
		putchar(zig);
	}
	if (preg != NULL) {
		/* Iteration over regexp matches, for color output.
		 * First regexec is a normal one, and then loop with
		 * REG_NOTBOL to not match "^pattern" anymore. */
		regmatch_t match;
		int regexec_flags = 0;
		while ((*p != '\0') && !regexec(preg, p, 1, &match, regexec_flags)) {
			if (match.rm_so > 0)
				printf("%.*s", match.rm_so, p);
			if (match.rm_eo > match.rm_so) {
				printf("%s%.*s%s", RED, match.rm_eo - match.rm_so, p + match.rm_so, NORM);
				p += match.rm_eo;
			} else {
				p += match.rm_eo;
				putchar(*p++);
			}
			regexec_flags = REG_NOTBOL;
		}
	} else if (searchfunc != NULL && searchstr != NULL) {
		/* Iteration over substring matches, for color output. */
		char *q;
		int searchlen = strlen(searchstr);
		while (searchlen && ((q = searchfunc(p, searchstr)) != NULL)) {
			if (p < q)
				printf("%.*s", (int)(q - p), p);
			printf("%s%.*s%s", RED, searchlen, q, NORM);
			p = q + searchlen;
		}
	}
	/* No color output (for context lines, or trailing portion
	 * of matching lines). */
	printf("%s\n", p);
	/* Once a line has been displayed, it is not valid anymore */
	current->valid = 0;
}
#define qgrep_print_context_line(buf, label, lineno) \
	qgrep_print_line(buf, label, lineno, '-', NULL, NULL, NULL)
#define qgrep_print_matching_line_nocolor(buf, label, lineno) \
	qgrep_print_line(buf, label, lineno, ':', NULL, NULL, NULL)
#define qgrep_print_matching_line_regcolor(buf, label, lineno, preg) \
	qgrep_print_line(buf, label, lineno, ':', preg, NULL, NULL)
#define qgrep_print_matching_line_strcolor(buf, label, lineno, searchfunc, searchstr) \
	qgrep_print_line(buf, label, lineno, ':', NULL, searchfunc, searchstr)

/* Display a leading context (valid lines of the buffers list, but the matching one). */
void qgrep_print_before_context(qgrep_buf_t *, const char, const char *, const int);
void qgrep_print_before_context(qgrep_buf_t *current, const char num_lines_before,
		const char *label, const int match_line_number)
{
	int line_number;
	line_number = match_line_number - num_lines_before;
	while ((current = current->next)
			&& (line_number < match_line_number)) {
		if (current->valid)
			qgrep_print_context_line(current, label, line_number);
		line_number++;
	}
}

/* Yield the path of one of the installed ebuilds (from VDB). */
char *get_next_installed_ebuild(char *, DIR *, struct dirent **, DIR **);
char *get_next_installed_ebuild(char *ebuild_path, DIR *vdb_dir, struct dirent **cat_dirent_pt, DIR **cat_dir_pt)
{
	struct dirent *pkg_dirent = NULL;
	if (*cat_dirent_pt == NULL || *cat_dir_pt == NULL)
		goto get_next_category;
get_next_ebuild_from_category:
	if ((pkg_dirent = readdir(*cat_dir_pt)) == NULL)
		goto get_next_category;
	if (pkg_dirent->d_name[0] == '.')
		goto get_next_ebuild_from_category;
	snprintf(ebuild_path, _Q_PATH_MAX, "%s/%s/%s.ebuild",
			(*cat_dirent_pt)->d_name, pkg_dirent->d_name, pkg_dirent->d_name);
	return ebuild_path;
get_next_category:
	if (*cat_dir_pt != NULL)
		closedir(*cat_dir_pt);
	*cat_dirent_pt = q_vdb_get_next_dir(vdb_dir);
	if (*cat_dirent_pt == NULL)
		return NULL;
	if ((*cat_dir_pt = opendir((*cat_dirent_pt)->d_name)) == NULL)
		goto get_next_category;
	goto get_next_ebuild_from_category;
}

int qgrep_main(int argc, char **argv)
{
	int i;
	int count = 0;
	char *p;
	char do_count, do_regex, do_eclass, do_installed, do_list;
	char show_filename, skip_comments, invert_list, show_name;
	char per_file_output;
	FILE *fp = NULL;
	DIR *eclass_dir = NULL;
	DIR *vdb_dir = NULL;
	DIR *cat_dir = NULL;
	struct dirent *dentry;
	char ebuild[_Q_PATH_MAX];
	char name[_Q_PATH_MAX];
	char *label;
	int reflags = 0;
	char invert_match = 0;
	regex_t preg, skip_preg;
	char *skip_pattern = NULL;
	depend_atom** include_atoms = NULL;
	unsigned long int context_optarg;
	char num_lines_before = 0;
	char num_lines_after = 0;
	qgrep_buf_t *buf_list;
	int need_separator = 0;
	char status = 1;

	QGREP_STR_FUNC strfunc = (QGREP_STR_FUNC) strstr;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	do_count = do_regex = do_eclass = do_installed = do_list = 0;
	show_filename = skip_comments = invert_list = show_name = 0;

	while ((i = GETOPT_LONG(QGREP, qgrep, "")) != -1) {
		switch (i) {
		case 'I': invert_match = 1; break;
		case 'i':
			strfunc = (QGREP_STR_FUNC) strcasestr;
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
		case 'J': do_installed = 1; break;
		case 'E': do_eclass = 1; break;
		case 'H': show_filename = 1; break;
		case 'N': show_name = 1; break;
		case 's': skip_comments = 1; break;
		case 'S': skip_pattern = optarg; break;
		case 'B':
		case 'A':
			errno = 0;
			context_optarg = strtol(optarg, &p, 10);
			if (errno != 0)
				errp("%s: not a valid integer", optarg);
			else if (p == optarg || *p != '\0')
				err("%s: not a valid integer", optarg);
			if (context_optarg > 254)
				err("%s: silly value!", optarg);
			if (i == 'B')
				num_lines_before = context_optarg;
			else
				num_lines_after = context_optarg;
			break;
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

	if (do_list && num_lines_before) {
		warn("%s and --before are incompatible options. The former wins.",
				(invert_list ? "--invert-list" : "--list"));
		num_lines_before = 0;
	}

	if (do_list && num_lines_after) {
		warn("%s and --after are incompatible options. The former wins.",
				(invert_list ? "--invert-list" : "--list"));
		num_lines_after = 0;
	}

	if (do_count && num_lines_before) {
		warn("--count and --before are incompatible options. The former wins.");
		num_lines_before = 0;
	}

	if (do_count && num_lines_after) {
		warn("--count and --after are incompatible options. The former wins.");
		num_lines_after = 0;
	}

	if (do_installed && do_eclass) {
		warn("--installed and --eclass are incompatible options. The former wins.");
		do_eclass = 0;
	}

	/* do we report results once per file or per line ? */
	per_file_output = do_count || (do_list && (!verbose || invert_list));
	/* label for prefixing matching lines or listing matching files */
	label = (show_name ? name : ((verbose || show_filename || do_list) ? ebuild : NULL));

	if (argc > (optind + 1)) {
		include_atoms = xcalloc(sizeof(depend_atom*), (argc - optind - 1));
		for (i = (optind + 1); i < argc; i++)
			if ((include_atoms[i - optind - 1] = atom_explode(argv[i])) == NULL)
				warn("%s: invalid atom, will be ignored", argv[i]);
	}

	/* pre-compile regexps once for all */
	if (do_regex) {
		if (invert_match || *RED == '\0')
			reflags |= REG_NOSUB;
		xregcomp(&preg, argv[optind], reflags);
		reflags |= REG_NOSUB;
		if (skip_pattern)
			xregcomp(&skip_preg, skip_pattern, reflags);
	}

	/* go look either in ebuilds or eclasses or VDB */
	if (!do_eclass && !do_installed) {
		initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
		if ((fp = fopen(CACHE_EBUILD_FILE, "r")) == NULL)
			return 1;
	} else if (do_eclass) {
		xchdir(portdir);
		if ((eclass_dir = opendir("eclass")) == NULL)
			errp("opendir(\"%s/eclass\") failed", portdir);
	} else { /* if (do_install) */
		char buf[_Q_PATH_MAX];
		snprintf(buf, sizeof(buf), "%s/%s", portroot, portvdb);
		xchdir(buf);
		if ((vdb_dir = opendir(".")) == NULL)
			errp("could not opendir(%s/%s) for ROOT/VDB", portroot, portvdb);
	}

	/* allocate a circular buffers list for --before */
	buf_list = qgrep_buf_list_alloc(num_lines_before + 1);

	/* iteration is either over ebuilds or eclasses */
	while (do_eclass
			? ((dentry = readdir(eclass_dir))
				&& snprintf(ebuild, sizeof(ebuild), "eclass/%s", dentry->d_name))
			: (do_installed
				? (get_next_installed_ebuild(ebuild, vdb_dir, &dentry, &cat_dir) != NULL)
				: (fgets(ebuild, sizeof(ebuild), fp) != NULL))) {
		FILE *newfp;

		/* filter badly named files, prepare eclass or package name, etc. */
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

		/* filter the files we grep when there are extra args */
		if (include_atoms != NULL)
			if (!qgrep_name_match(name, (argc - optind - 1), include_atoms))
				continue;

		if ((newfp = fopen(ebuild, "r")) != NULL) {
			int lineno = 0;
			char remaining_after_context = 0;
			count = 0;
			/* if there have been some matches already, then a separator will be needed */
			need_separator = (!status) && (num_lines_before || num_lines_after);
			/* whatever is in the circular buffers list is no more a valid context */
			qgrep_buf_list_invalidate(buf_list);

			/* reading a new line always happen in the next buffer of the list */
			while ((buf_list = buf_list->next)
					&& (fgets(buf_list->buf, sizeof(buf_list->buf), newfp)) != NULL) {
				lineno++;
				buf_list->valid = 1;

				/* cleanup EOL */
				if ((p = strrchr(buf_list->buf, '\n')) != NULL)
					*p = 0;
				if ((p = strrchr(buf_list->buf, '\r')) != NULL)
					*p = 0;

				if (skip_comments) {
					/* reject comments line ("^[ \t]*#") */
					p = buf_list->buf;
					while (*p == ' ' || *p == '\t') p++;
					if (*p == '#')
						goto print_after_context;
				}

				if (skip_pattern) {
					/* reject some other lines which match an optional pattern */
					if (!do_regex) {
						if (strfunc(buf_list->buf, skip_pattern) != NULL)
							goto print_after_context;
					} else {
						if (regexec(&skip_preg, buf_list->buf, 0, NULL, 0) == 0)
							goto print_after_context;
					}
				}

				/* four ways to match a line (with/without inversion and regexp) */
				if (!invert_match) {
					if (do_regex == 0) {
						if (strfunc(buf_list->buf, argv[optind]) == NULL)
							goto print_after_context;
					} else {
						if (regexec(&preg, buf_list->buf, 0, NULL, 0) != 0)
							goto print_after_context;
					}
				} else {
					if (do_regex == 0) {
						if (strfunc(buf_list->buf, argv[optind]) != NULL)
							goto print_after_context;
					} else {
						if (regexec(&preg, buf_list->buf, 0, NULL, 0) == 0)
							goto print_after_context;
					}
				}

				count++;
				status = 0; /* got a match, exit status should be 0 */
				if (per_file_output)
					continue; /* matching files are listed out of this loop */

				if ((need_separator > 0)
						&& (num_lines_before || num_lines_after))
					printf("--\n");
				/* "need_separator" is not a flag, but a counter, so that
				 * adjacent contextes are not separated */
				need_separator = 0 - num_lines_before;
				if (!do_list) {
					/* print the leading context */
					qgrep_print_before_context(buf_list, num_lines_before, label,
							((verbose > 1) ? lineno : -1));
					/* print matching line */
					if (invert_match || *RED == '\0')
						qgrep_print_matching_line_nocolor(buf_list, label,
							((verbose > 1) ? lineno : -1));
					else if (do_regex)
						qgrep_print_matching_line_regcolor(buf_list, label,
							((verbose > 1) ? lineno : -1), &preg);
					else
						qgrep_print_matching_line_strcolor(buf_list, label,
							((verbose > 1) ? lineno : -1), strfunc, argv[optind]);
				} else {
					/* in verbose do_list mode, list the file once per match */
					printf("%s", label);
					if (verbose > 1)
						printf(":%d", lineno);
					putchar('\n');
				}
				/* init count down of trailing context lines */
				remaining_after_context = num_lines_after;
				continue;

print_after_context:
				/* print some trailing context lines when needed */
				if (!remaining_after_context) {
					if (!status)
						/* we're getting closer to the need of a separator between
						 * current match block and the next one */
						++need_separator;
				} else {
					qgrep_print_context_line(buf_list, label,
							((verbose > 1) ? lineno : -1));
					--remaining_after_context;
				}
			}
			fclose(newfp);
			if (!per_file_output)
				continue; /* matches were already displayed, line per line */
			if (do_count && count) {
				if (label != NULL)
					/* -c without -v/-N/-H only outputs
					 * the matches count of the file */
					printf("%s:", label);
				printf("%d\n", count);
			} else if ((count && !invert_list) || (!count && invert_list))
				printf("%s\n", label); /* do_list == 1, or we wouldn't be here */
		}
	}
	if (do_eclass)
		closedir(eclass_dir);
	else if (!do_installed)
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
	qgrep_buf_list_free(buf_list);
	return status;
}

#else
DEFINE_APPLET_STUB(qgrep)
#endif
