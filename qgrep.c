/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qgrep.c,v 1.1 2005/11/06 18:07:17 solar Exp $
 *
 * Copyright 2005 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005 Mike Frysinger  - <vapier@gentoo.org>
 */

#define QGREP_FLAGS "Iice" COMMON_FLAGS
static struct option const qgrep_long_opts[] = {
	{"invert-match", no_argument, NULL, 'I'},
	{"ignore-case",  no_argument, NULL, 'i'},
	{"count",        no_argument, NULL, 'c'},
	{"regexp",       no_argument, NULL, 'e'},
	COMMON_LONG_OPTS
};
static const char *qgrep_opts_help[] = {
	"select non-matching lines",
	"ignore case distinctions",
	"only print a count of matching lines per FILE",
	"use PATTERN as a regular expression",
	COMMON_OPTS_HELP
};
#define qgrep_usage(ret) usage(ret, QGREP_FLAGS, qgrep_long_opts, qgrep_opts_help, APPLET_QGREP)

int qgrep_main(int argc, char **argv)
{
	int i;
	int count = 0;
	char *p;
	char do_count, do_regex;
        FILE *fp;
        char ebuild[_POSIX_PATH_MAX];
	char buf0[BUFSIZ];
	int reflags = REG_NOSUB;
	char invert_match = 0;

	typedef char *(*FUNC) (char *, char *);
	FUNC strfunc = (FUNC) strstr;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	do_count = do_regex = 0;

	while ((i = GETOPT_LONG(QGREP, qgrep, "")) != -1) {
		switch (i) {
		case 'I': invert_match = 1; break;
		case 'i':
			strfunc = (FUNC) strcasestr;
			reflags |= REG_ICASE;
			break;
		case 'c': do_count = 1; break;
		case 'e': do_regex = 1; break;
		COMMON_GETOPTS_CASES(qgrep)
		}
	}
	if (argc == optind)
		qgrep_usage(EXIT_FAILURE);

	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */

	if ((fp = fopen(".ebuild.x", "r")) == NULL)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			unsigned int lineno = 0;
			count = 0;
			while ((fgets(buf0, sizeof(buf0), newfp)) != NULL) {
				lineno++;
				if ((p = strrchr(buf0, '\n')) != NULL)
					*p = 0;
				if ((p = strrchr(buf0, '\r')) != NULL)
					*p = 0;

				if (!invert_match) {
					if (do_regex == 0) {
						if (( (FUNC *) (strfunc) (buf0, argv[optind])) == NULL) continue;
					} else {
						if ((rematch(argv[optind], buf0, reflags)) != 0) continue;
					}
				} else {
					if (do_regex == 0) {
						if (( (FUNC *) (strfunc) (buf0, argv[optind])) != NULL) continue;
					} else {
						if ((rematch(argv[optind], buf0, reflags)) == 0) continue;
					}
				}

				count++;
				if (do_count) continue;
				if (verbose) {
					printf("%s:", ebuild); 
					if (verbose > 1) printf("%d:", lineno);
					printf(" ");
				}
				printf("%s\n",  buf0);
			}
			fclose(newfp);
			if (do_count && count) {
				printf("%d", count);
				if (verbose) printf(" %s", ebuild);
				puts("");
			}
		}
	}
	fclose(fp);
	return EXIT_SUCCESS;
}
