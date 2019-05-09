/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005      Petteri Räty    - <betelgeuse@gentoo.org>
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <xalloc.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "atom.h"
#include "tree.h"
#include "xarray.h"
#include "xchdir.h"
#include "xregex.h"

#define QGREP_FLAGS "IiHNclLexJEsRS:B:A:" COMMON_FLAGS
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
	{"repo",          no_argument, NULL, 'R'},
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
	"Print source repository name for each match (implies -N)",
	"Skip lines matching <arg>",
	"Print <arg> lines of leading context",
	"Print <arg> lines of trailing context",
	COMMON_OPTS_HELP
};
#define qgrep_usage(ret) usage(ret, QGREP_FLAGS, qgrep_long_opts, qgrep_opts_help, NULL, lookup_applet_idx("qgrep"))

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
static qgrep_buf_t *
qgrep_buf_list_alloc(const char length)
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
static void
qgrep_buf_list_free(qgrep_buf_t *head)
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
static void
qgrep_buf_list_invalidate(qgrep_buf_t *head)
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
static void
qgrep_print_line(qgrep_buf_t *current, const char *label,
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
				printf("%.*s", (int)match.rm_so, p);
			if (match.rm_eo > match.rm_so) {
				printf("%s%.*s%s", RED, (int)(match.rm_eo - match.rm_so),
						p + match.rm_so, NORM);
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
static void
qgrep_print_before_context(qgrep_buf_t *current, const char num_lines_before,
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

struct qgrep_grepargs {
	bool do_count:1;
	bool do_regex:1;
	bool do_list:1;
	bool show_filename:1;
	bool show_name:1;
	bool show_repo:1;
	bool skip_comments:1;
	bool invert_list:1;
	bool invert_match:1;
	char *skip_pattern;
	char num_lines_before;
	char num_lines_after;
	qgrep_buf_t *buf_list;
	regex_t skip_preg;
	regex_t preg;
	const char *query;
	QGREP_STR_FUNC strfunc;
	depend_atom **include_atoms;
	const char *portdir;
};

static int
qgrep_grepat(int fd, const char *file, const char *label,
		struct qgrep_grepargs *a)
{
	FILE *newfp;
	int need_separator = 0;
	int count = 0;
	int lineno = 0;
	char remaining_after_context = 0;
	char status = 1;
	char *p;
	bool per_file_output;

	/* do we report results once per file or per line ? */
	per_file_output =
		a->do_count || (a->do_list && (!verbose || a->invert_list));

	if (fd >= 0) {
		int sfd = openat(fd, file, O_RDONLY|O_CLOEXEC);
		newfp = sfd >= 0 ? fdopen(sfd, "r") : NULL;
	} else {
		newfp = fopen(file, "r");
	}
	if (newfp == NULL)
		return status;

	count = 0;
	/* if there have been some matches already, then a
	 * separator will be needed */
	need_separator =
		!status && (a->num_lines_before || a->num_lines_after);
	/* whatever is in the circular buffers list is no more a
	 * valid context */
	qgrep_buf_list_invalidate(a->buf_list);

	/* reading a new line always happen in the next buffer
	 * of the list */
	while ((a->buf_list = a->buf_list->next) &&
			fgets(a->buf_list->buf, sizeof(a->buf_list->buf), newfp))
	{
		lineno++;
		a->buf_list->valid = 1;

		/* cleanup EOL */
		if ((p = strrchr(a->buf_list->buf, '\n')) != NULL)
			*p = 0;
		if ((p = strrchr(a->buf_list->buf, '\r')) != NULL)
			*p = 0;

		if (a->skip_comments) {
			/* reject comments line ("^[ \t]*#") */
			p = a->buf_list->buf;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '#')
				goto print_after_context;
		}

		if (a->skip_pattern) {
			/* reject some other lines which match an
			 * optional pattern */
			if (!a->do_regex) {
				if (a->strfunc(a->buf_list->buf, a->skip_pattern) != NULL)
					goto print_after_context;
			} else {
				if (regexec(&a->skip_preg, a->buf_list->buf,
							0, NULL, 0) == 0)
					goto print_after_context;
			}
		}

		/* four ways to match a line (with/without inversion
		 * and regexp) */
		if (!a->invert_match) {
			if (a->do_regex == 0) {
				if (a->strfunc(a->buf_list->buf, a->query) == NULL)
					goto print_after_context;
			} else {
				if (regexec(&a->preg, a->buf_list->buf, 0, NULL, 0) != 0)
					goto print_after_context;
			}
		} else {
			if (a->do_regex == 0) {
				if (a->strfunc(a->buf_list->buf, a->query) != NULL)
					goto print_after_context;
			} else {
				if (regexec(&a->preg, a->buf_list->buf, 0, NULL, 0) == 0)
					goto print_after_context;
			}
		}

		count++;
		status = 0; /* got a match, exit status should be 0 */
		if (per_file_output)
			continue;
		/* matching files are listed out of this loop */

		if ((need_separator > 0)
				&& (a->num_lines_before || a->num_lines_after))
			printf("--\n");
		/* "need_separator" is not a flag, but a counter, so that
		 * adjacent contextes are not separated */
		need_separator = 0 - a->num_lines_before;
		if (!a->do_list) {
			/* print the leading context */
			qgrep_print_before_context(a->buf_list,
					a->num_lines_before, label,
					((verbose > 1) ? lineno : -1));
			/* print matching line */
			if (a->invert_match || *RED == '\0')
				qgrep_print_matching_line_nocolor(a->buf_list, label,
						((verbose > 1) ? lineno : -1));
			else if (a->do_regex)
				qgrep_print_matching_line_regcolor(a->buf_list, label,
						((verbose > 1) ? lineno : -1), &a->preg);
			else
				qgrep_print_matching_line_strcolor(a->buf_list, label,
						((verbose > 1) ? lineno : -1), a->strfunc,
						a->query);
		} else {
			/* in verbose do_list mode, list the file once
			 * per match */
			printf("%s", label);
			if (verbose > 1)
				printf(":%d", lineno);
			putchar('\n');
		}
		/* init count down of trailing context lines */
		remaining_after_context = a->num_lines_after;
		continue;

print_after_context:
		/* print some trailing context lines when needed */
		if (!remaining_after_context) {
			if (!status)
				/* we're getting closer to the need of a
				 * separator between current match block and
				 * the next one */
				++need_separator;
		} else {
			qgrep_print_context_line(a->buf_list, label,
					((verbose > 1) ? lineno : -1));
			--remaining_after_context;
		}
	}
	fclose(newfp);
	if (per_file_output) {
		/* matches were already displayed, line per line */
		if (a->do_count && count) {
			if (label != NULL)
				/* -c without -v/-N/-H only outputs
				 * the matches count of the file */
				printf("%s:", label);
			printf("%d\n", count);
		} else if ((count && !a->invert_list) ||
				(!count && a->invert_list))
		{
			printf("%s\n", label);
		}
		/* do_list == 1, or we wouldn't be here */
	}

	return status;
}

static int
qgrep_cache_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qgrep_grepargs *data = (struct qgrep_grepargs *)priv;
	char buf[_Q_PATH_MAX];
	char name[_Q_PATH_MAX];
	char *label;
	depend_atom *patom = NULL;
	tree_ctx *cctx;
	int ret;
	int pfd;

	snprintf(buf, sizeof(buf), "%s/%s",
			pkg_ctx->cat_ctx->name, pkg_ctx->name);
	patom = atom_explode(buf);
	if (patom == NULL)
		return EXIT_FAILURE;

	if (data->include_atoms != NULL) {
		depend_atom **d;
		for (d = data->include_atoms; *d != NULL; d++) {
			if (atom_compare(patom, *d) == EQUAL)
				break;
		}
		if (*d == NULL) {
			atom_implode(patom);
			return EXIT_FAILURE;
		}
	}

	/* need to construct path in portdir to ebuild, pass it to grep */
	cctx = (tree_ctx *)(pkg_ctx->cat_ctx->ctx);
	if (cctx->cachetype == CACHE_EBUILD) {
		pfd = cctx->tree_fd;
	} else {
		pfd = openat(cctx->tree_fd, "../..", O_RDONLY|O_CLOEXEC);
	}

	/* cat/pkg/pkg-ver.ebuild */
	snprintf(buf, sizeof(buf), "%s/%s/%s.ebuild",
			patom->CATEGORY, patom->PN, patom->P);

	label = NULL;
	if (data->show_name) {
		char *repo = data->show_repo ? cctx->repo : NULL;
		snprintf(name, sizeof(name), "%s%s/%s%s%s%s%s%s",
				BOLD, patom->CATEGORY, BLUE, patom->P, GREEN,
				repo ? "::" : "", repo ? repo : "", NORM);
		label = name;
	} else if (data->show_filename) {
		label = buf;
	}

	ret = qgrep_grepat(pfd, buf, label, data);

	atom_implode(patom);

	return ret;
}

static int
qgrep_vdb_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct qgrep_grepargs *data = (struct qgrep_grepargs *)priv;
	char buf[_Q_PATH_MAX];
	char name[_Q_PATH_MAX];
	char *label;
	depend_atom *patom = NULL;
	int ret;
	int pfd;

	snprintf(buf, sizeof(buf), "%s/%s",
			pkg_ctx->cat_ctx->name, pkg_ctx->name);
	patom = atom_explode(buf);
	if (patom == NULL)
		return EXIT_FAILURE;

	if (data->include_atoms != NULL) {
		depend_atom **d;
		for (d = data->include_atoms; *d != NULL; d++) {
			if (atom_compare(patom, *d) == EQUAL)
				break;
		}
		if (*d == NULL) {
			atom_implode(patom);
			return EXIT_FAILURE;
		}
	}

	/* get path to portdir */
	pfd = openat(pkg_ctx->cat_ctx->ctx->portroot_fd,
			data->portdir, O_RDONLY|O_CLOEXEC);

	/* cat/pkg/pkg-ver.ebuild */
	snprintf(buf, sizeof(buf), "%s/%s/%s.ebuild",
			patom->CATEGORY, patom->PN, patom->P);

	label = NULL;
	if (data->show_name) {
		snprintf(name, sizeof(name), "%s%s/%s%s%s",
				BOLD, patom->CATEGORY, BLUE, patom->P, NORM);
		label = name;
	} else if (data->show_filename) {
		label = buf;
	}

	ret = qgrep_grepat(pfd, buf, label, data);

	atom_implode(patom);

	return ret;
}

int qgrep_main(int argc, char **argv)
{
	int i;
	char *p;
	bool do_eclass;
	bool do_installed;
	DIR *eclass_dir = NULL;
	struct dirent *dentry = NULL;
	int reflags = 0;
	unsigned long int context_optarg;
	char status = 1;
	size_t n;
	char *overlay;

	struct qgrep_grepargs args = {
		.do_count = 0,
		.do_regex = 0,
		.do_list = 0,
		.show_filename = 0,
		.show_name = 0,
		.skip_comments = 0,
		.invert_list = 0,
		.invert_match = 0,
		.skip_pattern = NULL,
		.num_lines_before = 0,
		.num_lines_after = 0,
		.buf_list = NULL,
		.query = NULL,
		.strfunc = strstr,
		.include_atoms = NULL,
		.portdir = NULL,
	};

	do_eclass = do_installed = 0;

	while ((i = GETOPT_LONG(QGREP, qgrep, "")) != -1) {
		switch (i) {
		case 'I': args.invert_match = true;               break;
		case 'i':
			args.strfunc = strcasestr;
			reflags |= REG_ICASE;
			break;
		case 'c': args.do_count = true;                   break;
		case 'l': args.do_list = true;                    break;
		case 'L': args.do_list = args.invert_list = true; break;
		case 'e': args.do_regex = true;                   break;
		case 'x':
			args.do_regex = true;
			reflags |= REG_EXTENDED;
			break;
		case 'J': do_installed = true;                    break;
		case 'E': do_eclass = true;                       break;
		case 'H': args.show_filename = true;              break;
		case 'N': args.show_name = true;                  break;
		case 's': args.skip_comments = true;              break;
		case 'R': args.show_repo = args.show_name = true; break;
		case 'S': args.skip_pattern = optarg;             break;
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
				args.num_lines_before = context_optarg;
			else
				args.num_lines_after = context_optarg;
			break;
		COMMON_GETOPTS_CASES(qgrep)
		}
	}
	if (argc == optind)
		qgrep_usage(EXIT_FAILURE);

	if (args.do_list && args.do_count) {
		warn("%s and --count are incompatible options. The former wins.",
				(args.invert_list ? "--invert-list" : "--list"));
		args.do_count = false;
	}

	if (args.show_name && args.show_filename) {
		warn("--with-name and --with-filename are incompatible options. "
				"The former wins.");
		args.show_filename = false;
	}

	if (args.do_list && args.num_lines_before) {
		warn("%s and --before are incompatible options. The former wins.",
				(args.invert_list ? "--invert-list" : "--list"));
		args.num_lines_before = 0;
	}

	if (args.do_list && args.num_lines_after) {
		warn("%s and --after are incompatible options. The former wins.",
				(args.invert_list ? "--invert-list" : "--list"));
		args.num_lines_after = 0;
	}

	if (args.do_count && args.num_lines_before) {
		warn("--count and --before are incompatible options. The former wins.");
		args.num_lines_before = 0;
	}

	if (args.do_count && args.num_lines_after) {
		warn("--count and --after are incompatible options. The former wins.");
		args.num_lines_after = 0;
	}

	if (do_installed && do_eclass) {
		warn("--installed and --eclass are incompatible options. "
				"The former wins.");
		do_eclass = false;
	}

	if (argc > (optind + 1)) {
		depend_atom **d = args.include_atoms =
			xcalloc(sizeof(depend_atom *), (argc - optind - 1) + 1);
		for (i = (optind + 1); i < argc; i++) {
			*d = atom_explode(argv[i]);
			if (*d == NULL) {
				warn("%s: invalid atom, will be ignored", argv[i]);
			} else {
				d++;
			}
		}
		*d = NULL;
	}

	/* make it easier to see what needs to be printed */
	if (!args.show_name && (verbose || args.do_list))
		args.show_filename = true;

	/* pre-compile regexps once for all */
	if (args.do_regex) {
		if (args.invert_match || *RED == '\0')
			reflags |= REG_NOSUB;
		xregcomp(&args.preg, argv[optind], reflags);
		reflags |= REG_NOSUB;
		if (args.skip_pattern)
			xregcomp(&args.skip_preg, args.skip_pattern, reflags);
	}
	args.query = argv[optind];

	/* allocate a circular buffers list for --before */
	args.buf_list = qgrep_buf_list_alloc(args.num_lines_before + 1);

	array_for_each(overlays, n, overlay) {
		args.portdir = overlay;
		if (do_eclass) {
			char buf[_Q_PATH_MAX];
			char name[_Q_PATH_MAX];
			char *label;
			int efd;

			snprintf(buf, sizeof(buf), "%s/%s/eclass", portroot, overlay);
			efd = open(buf, O_RDONLY|O_CLOEXEC);
			if (efd == -1 || (eclass_dir = fdopendir(efd)) == NULL) {
				if (errno != ENOENT)
					warnp("opendir(\"%s/eclass\") failed", overlay);
				continue;
			}
			while ((dentry = readdir(eclass_dir)) != NULL) {
				if (strstr(dentry->d_name, ".eclass") == NULL)
					continue;
				/* filter the files we grep when there are extra args */
				if (args.include_atoms != NULL) {
					depend_atom **d;
					for (d = args.include_atoms; *d != NULL; d++) {
						if ((*d)->PN != NULL && strncmp(dentry->d_name,
									(*d)->PN, strlen((*d)->PN)) == 0)
							break;
					}
					if (*d == NULL)
						continue;
				}

				label = NULL;
				if (args.show_name) {
					snprintf(name, sizeof(name), "%s%.*s%s", BLUE,
							(int)(strlen(dentry->d_name) - 7), dentry->d_name,
							NORM);
					label = name;
				} else if (args.show_filename) {
					snprintf(name, sizeof(name), "eclass/%s", dentry->d_name);
					label = name;
				}
				status = qgrep_grepat(efd, dentry->d_name, label, &args);
			}
			closedir(eclass_dir);
		} else if (do_installed) {
			tree_ctx *t = tree_open_vdb(portroot, portvdb);
			if (t != NULL) {
				status = tree_foreach_pkg_fast(t, qgrep_vdb_cb, &args, NULL);
				tree_close(t);
			}
		} else { /* do_ebuild */
			tree_ctx *t = tree_open(portroot, overlay);
			if (t != NULL) {
				status = tree_foreach_pkg_fast(t, qgrep_cache_cb, &args, NULL);
				tree_close(t);
			}
		}
	}

	if (args.do_regex)
		regfree(&args.preg);
	if (args.do_regex && args.skip_pattern)
		regfree(&args.skip_preg);
	if (args.include_atoms != NULL) {
		for (i = 0; i < (argc - optind - 1); i++)
			if (args.include_atoms[i] != NULL)
				atom_implode(args.include_atoms[i]);
		free(args.include_atoms);
	}
	qgrep_buf_list_free(args.buf_list);

	return status;
}
