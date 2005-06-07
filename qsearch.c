/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsearch.c,v 1.1 2005/06/07 02:17:24 solar Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */


/* functional description of search options based on emerge(1) */

/* --searchdesc (-S) */
char qsearch_searchdesc_help[] =
	"Matches the search string against the description field as well as the package name.\n"
	"Take caution as the descriptions are also matched as regular expressions.\n";

/* --search (-s) */
char qsearch_search_help[] =
	"Searches for matches of the supplied string in the portage tree.\n"
	"The --search string is a regular expression.\n"
	"For example, qsearch --search '^kde' searches for any package that starts with 'kde'\n"
	"qsearch --search 'gcc$' searches for any package that ends with 'gcc'\n"
	"qsearch --search 'office' searches for any package that contains the word 'office'.\n"
	"If you want to search the package descriptions as well, use the --searchdesc option.\n";

#define QSEARCH_FLAGS "asS" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"all",        no_argument, NULL, 'a'},
	{"search",     no_argument, NULL, 's'},
	{"searchdesc",  a_argument, NULL, 'S'},
	COMMON_LONG_OPTS
};
static const char *qsearch_opts_help[] = {
	"List the descriptions of every package in the cache",
	"Regex search package names",
	"Regex search package descriptions",
	COMMON_OPTS_HELP
};

#define qsearch_usage(ret) usage(ret, QSEARCH_FLAGS, qsearch_long_opts, qsearch_opts_help, QSEARCH_IDX)

int qsearch_main(int argc, char **argv)
{
	FILE *fp;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];
	char last[126];
	char *p, *q, *str;
	char *search_me = NULL;
	char search_desc = 1, search_all = 0;
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	last[0] = 0;
	initialize_ebuild_flat();
	fp = fopen(".ebuild.x", "r");
	if (!fp)
		return 1;

	while ((i=getopt_long(argc, argv, QSEARCH_FLAGS, qsearch_long_opts, NULL)) != -1) {
		switch (i) {

		case 'V': version_barf(); break;
		case 'h': qsearch_usage(EXIT_SUCCESS); break;
		case 'a': search_all = 1; break;
		case 's': search_desc = 0; break;
		case 'S': search_desc = 1;
		}
	}

	if (!search_all) {
		argc = 0;
		search_desc = 1;
	} else {
		if (argc == optind)
			qsearch_usage(EXIT_FAILURE);
		search_me = argv[optind];
	}

	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {

		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if (!ebuild[0])
			continue;
		str = strdup(ebuild);
		p = (char *) dirname(str);

		if ((strcmp(p, last)) != 0) {
			FILE *newfp;
			strncpy(last, p, sizeof(last));
			if (!search_desc) {
				if ((rematch(search_me, basename(last), REG_EXTENDED | REG_ICASE)) == 0)
					printf("%s\n", last);
				continue;
			}
			if ((newfp = fopen(ebuild, "r")) != NULL) {
				while ((fgets(buf, sizeof(buf), newfp)) != NULL) {
					if ((strlen(buf) <= 13))
						continue;
					if ((strncmp(buf, "DESCRIPTION=", 12)) == 0) {
						if ((q = strrchr(buf, '"')) != NULL)
							*q = 0;
						if (strlen(buf) <= 12)
							break;
						q = buf + 13;
						if (argc > 1) {
							if ((rematch(search_me, q, REG_EXTENDED | REG_ICASE)) == 0) {
								fprintf(stdout, "%s %s\n", p, q);
							}
						} else {
							fprintf(stdout, "%s %s\n", p, q);
						}
						break;
					}
				}
				fclose(newfp);
			} else {
				if (!reinitialize)
					warnf("(cache update pending) %s : %s", ebuild, strerror(errno));
				reinitialize = 1;
			}
		}
		free(str);
	}
	fclose(fp);
	return EXIT_SUCCESS;
}
