/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsearch.c,v 1.11 2005/06/21 02:31:48 vapier Exp $
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

#if 0
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
#endif



#define QSEARCH_FLAGS "acsSH" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"all",       no_argument, NULL, 'a'},
	{"cache",     no_argument, NULL, 'c'},
	{"search",    no_argument, NULL, 's'},
	{"desc",       a_argument, NULL, 'S'},
	{"homepage",  no_argument, NULL, 'H'},
	COMMON_LONG_OPTS
};
static const char *qsearch_opts_help[] = {
	"List the descriptions of every package in the cache",
	"Use the portage cache",
	"Regex search package names",
	"Regex search package descriptions",
	"Show homepage info",
	COMMON_OPTS_HELP
};
#define qsearch_usage(ret) usage(ret, QSEARCH_FLAGS, qsearch_long_opts, qsearch_opts_help, APPLET_QSEARCH)



int qsearch_main(int argc, char **argv)
{
	FILE *fp;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];
	char last[126];
	char *p, *q, *str;
	char *search_me = NULL;
	char show_homepage = 0;
	char search_desc = 1, search_all = 0, search_cache = CACHE_EBUILD;
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QSEARCH, qsearch, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsearch)
		case 'a': search_all = 1; break;
		case 'c': search_cache = CACHE_METADATA; break;
		case 's': search_desc = 0; break;
		case 'S': search_desc = 1; break;
		case 'H': show_homepage = 1; break;
		}
	}

	if (search_all) {
		search_desc = 1;
	} else {
		if (argc == optind)
			qsearch_usage(EXIT_FAILURE);
		search_me = argv[optind];
	}

	last[0] = 0;
	fp = fopen(initialize_flat(search_cache), "r");
	if (!fp)
		return 1;

	while (fgets(ebuild, sizeof(ebuild), fp) != NULL) {
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if (!ebuild[0])
			continue;

		switch (search_cache) {

		case CACHE_METADATA: {
			portage_cache *pcache;
			if ((pcache = cache_read_file(ebuild)) != NULL) {
				if ((strcmp(pcache->atom->PN, last)) != 0) {
					strncpy(last, pcache->atom->PN, sizeof(last));
					if ((rematch(search_me, (search_desc ? pcache->DESCRIPTION : ebuild), REG_EXTENDED | REG_ICASE)) == 0)
						printf("%s%s/%s%s%s %s\n", BOLD, pcache->atom->CATEGORY, BLUE,
						       pcache->atom->PN, NORM, (show_homepage ? pcache->HOMEPAGE : pcache->DESCRIPTION));
				}
				cache_free(pcache);
			} else {
				if (!reinitialize)
					warnf("(cache update pending) %s", ebuild);
				reinitialize = 1;
			}
			break;
		}

		case CACHE_EBUILD: {
			FILE *ebuildfp;
			str = xstrdup(ebuild);
			p = (char *)dirname(str);

			if ((strcmp(p, last)) != 0) {
				strncpy(last, p, sizeof(last));
				if (!search_desc) {
					if ((rematch(search_me, basename(last), REG_EXTENDED | REG_ICASE)) == 0)
						printf("%s\n", last);
					continue;
				}
				if ((ebuildfp = fopen(ebuild, "r")) != NULL) {
					const char *search_vars[] = { "HOMEPAGE=", "DESCRIPTION=" };
					size_t search_len = (show_homepage ? 9 : 12);
					const char *search_var = (show_homepage ? search_vars[0] : search_vars[1]);
					while ((fgets(buf, sizeof(buf), ebuildfp)) != NULL) {
						if (strlen(buf) <= search_len)
							continue;
						if (strncmp(buf, search_var, search_len) == 0) {
							if ((q = strrchr(buf, '"')) != NULL)
								*q = 0;
							if (strlen(buf) <= search_len)
								break;
							q = buf + search_len + 1;
							if (!search_all && rematch(search_me, q, REG_EXTENDED | REG_ICASE) != 0)
								break;
							printf("%s%s/%s%s%s %s\n", 
								BOLD, dirname(p), BLUE, basename(p), NORM, q);
							break;
						}
					}
					fclose(ebuildfp);
				} else {
					if (!reinitialize)
						warnfp("(cache update pending) %s", ebuild);
					reinitialize = 1;
				}
			}
			free(str);

			break;
		} /* case CACHE_EBUILD */
		} /* switch (search_cache) */
	}
	fclose(fp);
	return EXIT_SUCCESS;
}
