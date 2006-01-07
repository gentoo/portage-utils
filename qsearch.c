/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsearch.c,v 1.26 2006/01/07 16:25:28 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qsearch

#define QSEARCH_FLAGS "acsSNH" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"all",       no_argument, NULL, 'a'},
	{"cache",     no_argument, NULL, 'c'},
	{"search",    no_argument, NULL, 's'},
	{"desc",       a_argument, NULL, 'S'},
	{"name-only", no_argument, NULL, 'N'},
	{"homepage",  no_argument, NULL, 'H'},
	COMMON_LONG_OPTS
};
static const char *qsearch_opts_help[] = {
	"List the descriptions of every package in the cache",
	"Use the portage cache",
	"Regex search package basenames",
	"Regex search package descriptions",
	"Only show package name",
	"Show homepage info",
	COMMON_OPTS_HELP
};
static const char qsearch_rcsid[] = "$Id: qsearch.c,v 1.26 2006/01/07 16:25:28 solar Exp $";
#define qsearch_usage(ret) usage(ret, QSEARCH_FLAGS, qsearch_long_opts, qsearch_opts_help, lookup_applet_idx("qsearch"))


int qsearch_main(int argc, char **argv)
{
	FILE *fp;
	char buf[_Q_PATH_MAX];
	char ebuild[_Q_PATH_MAX];
	char last[126] = "";
	char dp[126] = "";
	char bp[126] = "";
	char *p, *q, *str;
	char *search_me = NULL;
	char show_homepage = 0, show_name_only = 0;
	char search_desc = 0, search_all = 0, search_name = 1, search_cache = CACHE_EBUILD;
	const char *search_vars[] = { "DESCRIPTION=", "HOMEPAGE=" };
	size_t search_len;
	int i, idx=0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QSEARCH, qsearch, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsearch)
		case 'a': search_all = 1; break;
		case 'c': search_cache = CACHE_METADATA; break;
		case 's': search_desc = 0; search_name = 1; break;
		case 'S': search_desc = 1; search_name = 0; break;
		case 'N': show_name_only = 1; break;
		case 'H': show_homepage = 1, idx=1; break;
		}
	}

	if (search_all) {
		search_desc = 1;
		search_name = 0;
	} else {
		if (argc == optind)
			qsearch_usage(EXIT_FAILURE);
		search_me = argv[optind];
	}

	last[0] = 0;
	fp = fopen(initialize_flat(search_cache), "r");
	if (!fp)
		return 1;

	/* moved these outside of the loop for better asm generation */
	search_len = strlen(search_vars[idx]);

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
						       pcache->atom->PN, NORM,
						       (show_name_only ? "" :
						        (show_homepage ? pcache->HOMEPAGE : pcache->DESCRIPTION)));
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
			p = (char *) dirname(str);

			if ((strcmp(p, last)) != 0) {
				strncpy(last, p, sizeof(last));
				if (search_name)
					if ((rematch(search_me, basename(last), REG_EXTENDED | REG_ICASE)) != 0)
						continue;
				if ((ebuildfp = fopen(ebuild, "r")) != NULL) {
					while ((fgets(buf, sizeof(buf), ebuildfp)) != NULL) {
						if (strlen(buf) <= search_len)
							continue;
						if (strncmp(buf, search_vars[idx], search_len) == 0) {
							if ((q = strrchr(buf, '"')) != NULL)
								*q = 0;
							if (strlen(buf) <= search_len)
								break;
							q = buf + search_len + 1;
							if (!search_all && !search_name && rematch(search_me, q, REG_EXTENDED | REG_ICASE) != 0)
								break;
							// hppa/arm dirname(),basename() eats the ptr so we need to copy the variables before 
							// doing operations with them.
							strncpy(dp, p, sizeof(dp));
							strncpy(bp, p, sizeof(bp));
							printf("%s%s/%s%s%s %s\n", 
								BOLD, dirname(dp), BLUE, basename(bp), NORM,
								(show_name_only ? "" : q));
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


#else /* ! APPLET_qsearch */
int qsearch_main(int argc, char **argv) {
	errf("%s", err_noapplet);
}
#endif /* APPLET_qsearch */
