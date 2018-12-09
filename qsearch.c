/*
 * Copyright 2005-2018 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org
 */

#ifdef APPLET_qsearch

#define QSEARCH_FLAGS "acesSNH" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"all",       no_argument, NULL, 'a'},
	{"cache",     no_argument, NULL, 'c'},
	{"ebuilds",   no_argument, NULL, 'e'},
	{"search",    no_argument, NULL, 's'},
	{"desc",       a_argument, NULL, 'S'},
	{"name-only", no_argument, NULL, 'N'},
	{"homepage",  no_argument, NULL, 'H'},
	COMMON_LONG_OPTS
};
static const char * const qsearch_opts_help[] = {
	"List the descriptions of every package in the cache",
	"Use the portage cache",
	"Use the portage ebuild tree (default)",
	"Regex search package basenames",
	"Regex search package descriptions",
	"Only show package name",
	"Show homepage info",
	COMMON_OPTS_HELP
};
#define qsearch_usage(ret) usage(ret, QSEARCH_FLAGS, qsearch_long_opts, qsearch_opts_help, NULL, lookup_applet_idx("qsearch"))

#define LAST_BUF_SIZE 256

/* Search an ebuild's details via the metadata cache. */
static void
qsearch_ebuild_metadata(_q_unused_ int overlay_fd, const char *ebuild, const char *search_me, char *last,
                        bool search_desc, bool search_all, _q_unused_ bool search_name, bool show_name_only, bool show_homepage)
{
	portage_cache *pcache = cache_read_file(ebuild);

	if (pcache == NULL) {
		if (!reinitialize)
			warnf("(cache update pending) %s", ebuild);
		reinitialize = 1;
		return;
	}

	if (strcmp(pcache->atom->PN, last) != 0) {
		strncpy(last, pcache->atom->PN, LAST_BUF_SIZE);
		if (search_all || rematch(search_me, (search_desc ? pcache->DESCRIPTION : ebuild), REG_EXTENDED | REG_ICASE) == 0)
			printf("%s%s/%s%s%s%s%s\n", BOLD, pcache->atom->CATEGORY, BLUE,
			       pcache->atom->PN, NORM,
				   (show_name_only ? "" : " "),
			       (show_name_only ? "" :
			        (show_homepage ? pcache->HOMEPAGE : pcache->DESCRIPTION)));
	}
	cache_free(pcache);
}

/* Search an ebuild's details via the ebuild cache. */
static void
qsearch_ebuild_ebuild(int overlay_fd, const char *ebuild, const char *search_me, char *last,
                      _q_unused_ bool search_desc, bool search_all, bool search_name, bool show_name_only, _q_unused_ bool show_homepage)
{
	const char * const search_vars[] = { "DESCRIPTION=", "HOMEPAGE=" };
	const char *search_var = search_vars[show_homepage ? 1 : 0];
	size_t search_len = strlen(search_var);
	char *p, *q, *str;

	FILE *ebuildfp;
	str = xstrdup(ebuild);
	p = dirname(str);

	if (strcmp(p, last) == 0)
		goto no_cache_ebuild_match;

	bool show_it = false;
	strncpy(last, p, LAST_BUF_SIZE);
	if (search_name) {
		if (rematch(search_me, basename(last), REG_EXTENDED | REG_ICASE) != 0) {
			goto no_cache_ebuild_match;
		} else {
			q = NULL;
			show_it = true;
		}
	}

	int fd = openat(overlay_fd, ebuild, O_RDONLY|O_CLOEXEC);
	if (fd != -1) {
		ebuildfp = fdopen(fd, "r");
		if (ebuildfp == NULL) {
			close(fd);
			goto no_cache_ebuild_match;
		}
	} else {
		if (!reinitialize)
			warnfp("(cache update pending) %s", ebuild);
		reinitialize = 1;
		goto no_cache_ebuild_match;
	}

	char *buf = NULL;
	int linelen;
	size_t buflen;
	while ((linelen = getline(&buf, &buflen, ebuildfp)) >= 0) {
		if ((size_t)linelen <= search_len)
			continue;
		if (strncmp(buf, search_var, search_len) != 0)
			continue;
		if ((q = strrchr(buf, '"')) != NULL)
			*q = 0;
		if (strlen(buf) <= search_len)
			break;
		q = buf + search_len + 1;
		if (!search_all && !search_name && rematch(search_me, q, REG_EXTENDED | REG_ICASE) != 0)
			break;
		show_it = true;
		break;
	}

	if (show_it) {
		const char *pkg = basename(p);
		printf("%s%s/%s%s%s%s%s\n",
			BOLD, dirname(p), BLUE, pkg, NORM,
			(show_name_only ? "" : " "),
			(show_name_only ? "" : q ? : "<no DESCRIPTION found>"));
	}

	free(buf);
	fclose(ebuildfp);
 no_cache_ebuild_match:
	free(str);
}

int qsearch_main(int argc, char **argv)
{
	char last[LAST_BUF_SIZE];
	char *search_me = NULL;
	bool show_homepage = false, show_name_only = false;
	bool search_desc = false, search_all = false, search_name = true;
	int search_cache = CACHE_EBUILD;
	int i;
	void (*search_func)(int, const char *, const char *, char *last, bool, bool, bool, bool, bool);

	while ((i = GETOPT_LONG(QSEARCH, qsearch, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsearch)
		case 'a': search_all = true; break;
		case 'c': search_cache = CACHE_METADATA; break;
		case 'e': search_cache = CACHE_EBUILD; break;
		case 's': search_desc = false; search_name = true; break;
		case 'S': search_desc = true; search_name = false; break;
		case 'N': show_name_only = true; break;
		case 'H': show_homepage = true; break;
		}
	}

	switch (search_cache) {
	case CACHE_METADATA:
		search_func = qsearch_ebuild_metadata;
		break;
	case CACHE_EBUILD:
		search_func = qsearch_ebuild_ebuild;
		break;
	default:
		err("unknown cache %i", search_cache);
	}

	if (search_all) {
		search_desc = true;
		search_name = false;
	} else {
		if (argc == optind)
			qsearch_usage(EXIT_FAILURE);
		search_me = argv[optind];
	}
	last[0] = 0;

	int ret = 0;
	size_t n;
	const char *overlay;
	array_for_each(overlays, n, overlay) {
		FILE *fp = fopen(initialize_flat(overlay, search_cache, false), "r");
		if (!fp) {
			warnp("opening cache for %s failed", overlay);
			ret = 1;
			continue;
		}

		int overlay_fd = open(overlay, O_RDONLY|O_CLOEXEC|O_PATH);
		if (overlay_fd < 0) {
			fclose(fp);
			warnp("open failed: %s", overlay);
			ret = 1;
			continue;
		}

		int linelen;
		size_t buflen;
		char *buf = NULL;
		while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
			rmspace_len(buf, (size_t)linelen);
			if (!buf[0])
				continue;

			search_func(overlay_fd, buf, search_me, last, search_desc,
				search_all, search_name, show_name_only, show_homepage);
		}
		free(buf);
		close(overlay_fd);
		fclose(fp);
	}

	return ret;
}

#else
DEFINE_APPLET_STUB(qsearch)
#endif
