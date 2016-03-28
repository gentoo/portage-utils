/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2006 Thomas A. Cort - <tcort@gentoo.org>
 */

#ifdef APPLET_qcache

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/********************************************************************/
/* Required portage-utils stuff                                     */
/********************************************************************/

#define QCACHE_FLAGS "p:c:idtans" COMMON_FLAGS
static struct option const qcache_long_opts[] = {
	{"matchpkg", a_argument, NULL, 'p'},
	{"matchcat", a_argument, NULL, 'c'},
	{"imlate",  no_argument, NULL, 'i'},
	{"dropped", no_argument, NULL, 'd'},
	{"testing", no_argument, NULL, 't'},
	{"stats",   no_argument, NULL, 's'},
	{"all",     no_argument, NULL, 'a'},
	{"not",     no_argument, NULL, 'n'},
	COMMON_LONG_OPTS
};
static const char * const qcache_opts_help[] = {
	"match pkgname",
	"match catname",
	"list packages that can be marked stable on a given arch",
	"list packages that have dropped keywords on a version bump on a given arch",
	"list packages that have ~arch versions, but no stable versions on a given arch",
	"display statistics about the portage tree",
	"list packages that have at least one version keyworded for on a given arch",
	"list packages that aren't keyworded on a given arch.",
	COMMON_OPTS_HELP
};
#define qcache_usage(ret) usage(ret, QCACHE_FLAGS, qcache_long_opts, qcache_opts_help, NULL, lookup_applet_idx("qcache"))

/********************************************************************/
/* Structs                                                          */
/********************************************************************/

typedef struct {
	const char *overlay;
	const char *category;
	const char *package;
	const char *ebuild;
	portage_cache *cache_data;
	unsigned char cur;
	unsigned char num;
} qcache_data;

/********************************************************************/
/* Global Variables                                                 */
/********************************************************************/

static queue *arches;
static int archlist_count;
static size_t arch_longest_len;
const char status[3] = {'-', '~', '+'};
int qcache_skip, qcache_test_arch;
char *qcache_matchpkg = NULL, *qcache_matchcat = NULL;

/********************************************************************/
/* Enumerations                                                     */
/********************************************************************/

enum { none = 0, testing, stable, minus };

/********************************************************************/
/* Keyword functions                                                */
/********************************************************************/

/*
 * int decode_status(char c);
 *
 * Decode keyword status
 *
 * IN:
 *  char c - status to check
 * OUT:
 *  int - one of the following enum { none = 0, testing, stable, minus };
 */
_q_static
int decode_status(char c)
{
	switch (c) {
		case '-': return minus;
		case '~': return testing;
		default:  return stable;
	}
}

/*
 * int decode_arch(const char *arch);
 *
 * Decode the architecture string
 *
 * IN:
 *  const char *arch - name of an arch (alpha, amd64, ...)
 * OUT:
 *  int pos - location of arch in archlist[]
 */
_q_static
int decode_arch(const char *arch)
{
	queue *q = arches;
	int a;
	const char *p;

	p = arch;
	if (*p == '~' || *p == '-')
		p++;

	a = 0;
	while (q) {
		if (strcmp(q->name, p) == 0)
			return a;
		++a;
		q = q->next;
	}

	return -1;
}

/*
 * void print_keywords(char *category, char *ebuild, int *keywords);
 *
 * Prints the keywords to stdout
 *
 * IN:
 *  char *category - current category of the current package
 *  int *keywords - an array of keywords that coincides with archlist
 */
_q_static
void print_keywords(const char *category, const char *ebuild, int *keywords)
{
	queue *arch = arches;
	int a;
	char *package;

	package = xstrdup(ebuild);
	package[strlen(ebuild)-7] = '\0';

	printf("%s%s/%s%s%s ", BOLD, category, BLUE, package, NORM);
	for (a = 0; a < archlist_count; ++a) {
		switch (keywords[a]) {
			case stable:
				printf("%s%c%s%s ", GREEN, status[keywords[a]], arch->name, NORM);
				break;
			case testing:
				printf("%s%c%s%s ", YELLOW, status[keywords[a]], arch->name, NORM);
				break;
		}
		arch = arch->next;
	}

	printf("\n");
	free(package);
}

/*
 * int read_keywords(char *s, int *keywords);
 *
 * Read the KEYWORDS string and decode the values
 *
 * IN:
 *  char *s - a keywords string (ex: "alpha ~amd64 -x86")
 *  int *keywords - the output
 * ERR:
 *  int rc - -1 is returned on error (if !s || !keywords)
 */
_q_static
int read_keywords(char *s, int *keywords)
{
	char *arch, delim[2] = { ' ', '\0' };
	size_t slen;
	int a;

	if (!s)
		return -1;

	memset(keywords, 0, sizeof(*keywords) * archlist_count);

	slen = strlen(s);
	if (slen >= 2 && s[0] == '-' && s[1] == '*')
		for (a = 0; a < archlist_count; ++a)
			keywords[a] = minus;

	if (!slen)
		return 0;

	arch = strtok(s, delim);
	do {
		a = decode_arch(arch);
		if (a == -1)
			continue;
		keywords[a] = decode_status(arch[0]);
	} while ((arch = strtok(NULL, delim)));

	return 0;
}

/*
 * portage_cache *qcache_read_cache_file(const char *file);
 *
 * Read a file from the edb cache and store data in portage_cache.
 *
 * IN:
 *  const char *filename - cache file to read
 * OUT:
 *  portage_cache *pkg - cache data
 * ERR:
 *  NULL is returned when an error occurs.
 */
_q_static
portage_cache *qcache_read_cache_file(const char *filename)
{
	struct stat s;
	char *buf;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len, buflen, linelen;

	if ((f = fopen(filename, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0) {
		fclose(f);
		goto err;
	}

	buf = NULL;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);

	while ((linelen = getline(&buf, &buflen, f)) != -1) {
		rmspace_len(buf, linelen);

		if (strncmp(buf, "DEPEND=", 7) == 0)
			ret->DEPEND = xstrdup(buf + 7);

		if (strncmp(buf, "DESCRIPTION=", 12) == 0)
			ret->DESCRIPTION = xstrdup(buf + 12);

		if (strncmp(buf, "HOMEPAGE=", 9) == 0)
			ret->HOMEPAGE = xstrdup(buf + 9);

		if (strncmp(buf, "INHERITED=", 10) == 0)
			ret->INHERITED = xstrdup(buf + 10);

		if (strncmp(buf, "IUSE=", 4) == 0)
			ret->IUSE = xstrdup(buf + 4);

		if (strncmp(buf, "KEYWORDS=", 9) == 0)
			ret->KEYWORDS = xstrdup(buf + 9);

		if (strncmp(buf, "LICENSE=", 8) == 0)
			ret->LICENSE = xstrdup(buf + 8);

		if (strncmp(buf, "PDEPEND=", 8) == 0)
			ret->PDEPEND = xstrdup(buf + 8);

		if (strncmp(buf, "PROVIDE=", 8) == 0)
			ret->PROVIDE = xstrdup(buf + 8);

		if (strncmp(buf, "RDEPEND=", 8) == 0)
			ret->RDEPEND = xstrdup(buf + 8);

		if (strncmp(buf, "RESTRICT=", 9) == 0)
			ret->RESTRICT = xstrdup(buf + 9);

		if (strncmp(buf, "SLOT=", 5) == 0)
			ret->SLOT = xstrdup(buf + 5);

		if (strncmp(buf, "SRC_URI=", 8) == 0)
			ret->SRC_URI = xstrdup(buf + 8);
	}

	free(buf);
	ret->atom = atom_explode(filename);
	fclose(f);

	return ret;

 err:
	if (ret)
		cache_free(ret);
	return NULL;
}

/*
 * void qcache_free_data(portage_cache *cache);
 *
 * free()'s a portage_cache
 *
 * IN:
 *  portage_cache *cache - the portage_cache to be free()'d
 */
_q_static
void qcache_free_data(portage_cache *cache)
{
	int i;
	char **c;

	if (!cache)
		errf("Cache is empty !");

	for (i = 0, c = (char**) cache; i < 15; i++)
		if (c[i])
			free(c[i]);

	atom_implode(cache->atom);
	free(cache);
}

/********************************************************************/
/* Comparison functions                                             */
/********************************************************************/

/*
 * int qcache_vercmp(const void *x, const void *y);
 *
 * Compare 2 struct dirent d_name strings based with atom_compare_str().
 * Used with dirscan() to sort ebuild filenames by version.
 *
 * IN:
 *  2 (const struct dirent **) with d_name filled in
 * OUT:
 *  -1 (NEWER)
 *   1 (OLDER)
 *   0 (SAME)
 */
_q_static
int qcache_vercmp(const struct dirent **x, const struct dirent **y)
{
	switch (atom_compare_str((*x)->d_name, (*y)->d_name)) {
		case NEWER: return -1;
		case OLDER: return  1;
		default:    return  0;
	}
}

/********************************************************************/
/* Selection functions                                              */
/********************************************************************/

/*
 * int qcache_file_select(const struct dirent *entry);
 *
 * Selects filenames that do not begin with '.' and are not name "metadata.xml"
 *  or that file matches ".cpickle"
 *
 * IN:
 *  const struct dirent *entry - entry to check
 * OUT:
 *  int - 0 if filename begins with '.' or is "metadata.xml", otherwise 1
 */
_q_static
int qcache_file_select(const struct dirent *entry)
{
	return !(entry->d_name[0] == '.' || (strcmp(entry->d_name, "metadata.xml") == 0) || (strstr(entry->d_name, ".cpickle") != 0));
}

/*
 * int qcache_ebuild_select(const struct dirent *entry);
 *
 * Select filenames that end in ".ebuild"
 *
 * IN:
 *  const struct dirent *entry - entry to check
 * OUT:
 *  int - 1 if the filename ends in ".ebuild", otherwise 0
 */
_q_static
int qcache_ebuild_select(const struct dirent *entry)
{
	return ((strlen(entry->d_name) > 7) && !strcmp(entry->d_name+strlen(entry->d_name)-7, ".ebuild"));
}

/********************************************************************/
/* Traversal function                                               */
/********************************************************************/

_q_static void qcache_load_arches(const char *overlay);

/*
 * int qcache_traverse(void (*func)(qcache_data*));
 *
 * visit every version of every package of every category in the tree
 *
 * IN:
 *  void (*func)(qcache_data*) - function to call
 * OUT:
 *  int - 0 on success.
 * ERR:
 *  exit or return -1 on failure.
 */
_q_static
int qcache_traverse_overlay(void (*func)(qcache_data*), const char *overlay)
{
	qcache_data data = {
		.overlay = overlay,
	};
	char *catpath, *pkgpath, *ebuildpath, *cachepath;
	int i, j, k, len, num_cat, num_pkg, num_ebuild;
	struct dirent **categories, **packages, **ebuilds;

	xasprintf(&catpath, "%s/dep/%s", portedb, overlay);

	if (-1 == (num_cat = scandir(catpath, &categories, qcache_file_select, alphasort))) {
		errp("%s", catpath);
		free(catpath);
	}

	if (!num_cat)
		warn("%s is empty!", catpath);

	/* traverse categories */
	for (i = 0; i < num_cat; i++) {
		xasprintf(&pkgpath, "%s/%s", overlay, categories[i]->d_name);

		if (-1 == (num_pkg = scandir(pkgpath, &packages, qcache_file_select, alphasort))) {
			if (errno != ENOENT)
				warnp("Found a cache dir, but unable to process %s", pkgpath);
			free(categories[i]);
			free(pkgpath);
			continue;
		}

		if (qcache_matchcat) {
			if (strcmp(categories[i]->d_name, qcache_matchcat) != 0) {
				scandir_free(packages, num_pkg);
				free(categories[i]);
				free(pkgpath);
				continue;
			}
		}

		/* traverse packages */
		for (j = 0; j < num_pkg; j++) {
			xasprintf(&ebuildpath, "%s/%s/%s", overlay, categories[i]->d_name, packages[j]->d_name);

			if (-1 == (num_ebuild = scandir(ebuildpath, &ebuilds, qcache_ebuild_select, qcache_vercmp))) {
				/* Do not complain about spurious files */
				if (errno != ENOTDIR)
					warnp("%s", ebuildpath);
				free(packages[j]);
				free(ebuildpath);
				continue;
			}

			if (qcache_matchpkg) {
				if (strcmp(packages[j]->d_name, qcache_matchpkg) != 0) {
					scandir_free(ebuilds, num_ebuild);
					free(packages[j]);
					free(ebuildpath);
					continue;
				}
			}

			qcache_skip = 0;

			/* traverse ebuilds */
			data.num = num_ebuild;
			for (k = 0; k < num_ebuild; k++) {
				len = xasprintf(&cachepath, "%s/%s/%s", catpath, categories[i]->d_name, ebuilds[k]->d_name);
				cachepath[len - 7] = '\0'; /* remove ".ebuild" */

				data.category = categories[i]->d_name;
				data.package = packages[j]->d_name;
				data.ebuild = ebuilds[k]->d_name;
				data.cur = k + 1;
				data.cache_data = qcache_read_cache_file(cachepath);

				if (data.cache_data != NULL) {
					if (!qcache_skip)
						func(&data);

					qcache_free_data(data.cache_data);
				} else {
					static bool warned = false;
					if (!warned) {
						warned = true;
						warnp("unable to read cache '%s'\n"
						      "\tperhaps you need to `egencache -j 4` ?", cachepath);
					}
				}

				free(ebuilds[k]);
				free(cachepath);
			}

			free(packages[j]);
			free(ebuilds);
			free(ebuildpath);
		}

		free(categories[i]);
		free(packages);
		free(pkgpath);
	}

	free(categories);
	free(catpath);

	return 0;
}

_q_static
int qcache_traverse(void (*func)(qcache_data*))
{
	int ret;
	size_t n;
	const char *overlay;

	/* Preload all the arches. Not entirely correctly (as arches are bound
	 * to overlays if set), but oh well. */
	array_for_each(overlays, n, overlay)
		qcache_load_arches(overlay);

	ret = 0;
	array_for_each(overlays, n, overlay)
		ret |= qcache_traverse_overlay(func, overlay);

	func(NULL);

	return ret;
}

/********************************************************************/
/* functors                                                         */
/********************************************************************/

_q_static
void qcache_imlate(qcache_data *data)
{
	int *keywords;
	int a;

	if (!data)
		return;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	switch (keywords[qcache_test_arch]) {
		case stable:
			qcache_skip = 1;
		case none:
		case minus:
			break;

		default:
			for (a = 0; a < archlist_count && !qcache_skip; ++a) {
				if (keywords[a] != stable)
					continue;
				qcache_skip = 1;
				print_keywords(data->category, data->ebuild, keywords);
			}
	}
	free(keywords);
}

_q_static
void qcache_not(qcache_data *data)
{
	int *keywords;

	if (!data)
		return;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	if (keywords[qcache_test_arch] == testing || keywords[qcache_test_arch] == stable) {
		qcache_skip = 1;
	} else if (data->cur == data->num) {
		printf("%s%s/%s%s%s\n", BOLD, data->category, BLUE, data->package, NORM);
	}

	free(keywords);
}

_q_static
void qcache_all(qcache_data *data)
{
	int *keywords;

	if (!data)
		return;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	if (keywords[qcache_test_arch] == stable || keywords[qcache_test_arch] == testing) {
		qcache_skip = 1;
		printf("%s%s/%s%s%s\n", BOLD, data->category, BLUE, data->package, NORM);
	}

	free(keywords);
}

_q_static
void qcache_dropped(qcache_data *data)
{
	static int possible = 0;
	int *keywords, i;

	if (!data)
		return;

	if (data->cur == 1)
		possible = 0;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	if (keywords[qcache_test_arch] == testing || keywords[qcache_test_arch] == stable) {
		qcache_skip = 1;

		if (possible) {
			printf("%s%s/%s%s%s\n", BOLD, data->category, BLUE, data->package, NORM);
		}

		free(keywords);
		return;
	}

	if (!possible) {
		/* don't count newer versions with "-*" keywords */
		for (i = 0; i < archlist_count; ++i) {
			if (keywords[i] == stable || keywords[i] == testing) {
				possible = 1;
				break;
			}
		}
	}

	free(keywords);
}

_q_static
void qcache_stats(qcache_data *data)
{
	static time_t runtime;
	static queue *allcats;
	static const char *last_overlay;
	static int numpkg  = 0;
	static int numebld = 0;
	static int numcat;
	static int *packages_stable;
	static int *packages_testing;
	static int *current_package_keywords;
	static int *keywords;
	int a;

	/* Is this the last time we'll be called? */
	if (!data) {
		const char border[] = "------------------------------------------------------------------";

		printf("+%.*s+\n", 25, border);
		printf("|   general statistics    |\n");
		printf("+%.*s+\n", 25, border);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "architectures", NORM, BLUE, archlist_count, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "categories", NORM, BLUE, numcat, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "packages", NORM, BLUE, numpkg, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "ebuilds", NORM, BLUE, numebld, NORM);
		printf("+%.*s+\n\n", 25, border);

		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);
		printf("|%*skeyword distribution                          |\n",
			(int)arch_longest_len, "");
		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);
		printf("| %s%*s%s |%s%8s%s |%s%8s%s |%s%8s%s | %s%8s%s |\n",
			RED, (int)arch_longest_len, "architecture", NORM, RED, "stable", NORM,
			RED, "~arch", NORM, RED, "total", NORM, RED, "total/#pkgs", NORM);
		printf("| %*s |         |%s%8s%s |         |             |\n",
			(int)arch_longest_len, "", RED, "only", NORM);
		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);

		queue *arch = arches;
		for (a = 0; a < archlist_count; ++a) {
			printf("| %s%*s%s |", GREEN, (int)arch_longest_len, arch->name, NORM);
			printf("%s%8d%s |", BLUE, packages_stable[a], NORM);
			printf("%s%8d%s |", BLUE, packages_testing[a], NORM);
			printf("%s%8d%s |", BLUE, packages_testing[a]+packages_stable[a], NORM);
			printf("%s%11.2f%s%% |\n", BLUE, (100.0*(packages_testing[a]+packages_stable[a]))/numpkg, NORM);
			arch = arch->next;
		}

		printf("+%.*s+\n\n", (int)(arch_longest_len + 46), border);

		printf("Completed in ");
		print_seconds_for_earthlings(time(NULL) - runtime);
		printf("\n");

		free(packages_stable);
		free(packages_testing);
		free(keywords);
		free(current_package_keywords);
		free_sets(allcats);
		return;
	}

	if (last_overlay != data->overlay) {
		DIR *dir;
		struct dirent *de;
		char *catpath;

		runtime = time(NULL);

		xasprintf(&catpath, "%s/dep/%s", portedb, data->overlay);
		dir = opendir(catpath);
		while ((de = readdir(dir)))
			if (de->d_type == DT_DIR && de->d_name[0] != '.') {
				bool ok;
				allcats = add_set_unique(de->d_name, allcats, &ok);
				if (ok)
					++numcat;
			}
		closedir(dir);
		free(catpath);

		last_overlay = data->overlay;
	}

	if (!numpkg) {
		packages_stable          = xcalloc(archlist_count, sizeof(*packages_stable));
		packages_testing         = xcalloc(archlist_count, sizeof(*packages_testing));
		keywords                 = xcalloc(archlist_count, sizeof(*keywords));
		current_package_keywords = xcalloc(archlist_count, sizeof(*current_package_keywords));
	}

	if (data->cur == 1) {
		numpkg++;
		memset(current_package_keywords, 0, archlist_count * sizeof(*current_package_keywords));
	}
	++numebld;

	memset(keywords, 0, archlist_count * sizeof(*keywords));
	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		return;
	}

	for (a = 0; a < archlist_count; ++a) {
		switch (keywords[a]) {
			case stable:
				current_package_keywords[a] = stable;
				break;
			case testing:
				if (current_package_keywords[a] != stable)
					current_package_keywords[a] = testing;
			default:
				break;
		}
	}

	if (data->cur == data->num) {
		for (a = 0; a < archlist_count; ++a) {
			switch (current_package_keywords[a]) {
				case stable:
					packages_stable[a]++;
					break;
				case testing:
					packages_testing[a]++;
				default:
					break;
			}
		}
	}
}

_q_static
void qcache_testing_only(qcache_data *data)
{
	static int possible = 0;
	int *keywords;

	if (!data)
		return;

	if (data->cur == 1)
		possible = 0;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	if (keywords[qcache_test_arch] == stable) {
		qcache_skip = 1;
		free(keywords);
		return;
	}

	/* the qcache_test_arch must have at least 1 ~arch keyword */
	if (keywords[qcache_test_arch] == testing)
		possible = 1;

	if (data->cur == data->num && possible) {
		printf("%s%s/%s%s%s\n", BOLD, data->category, BLUE, data->package, NORM);
	}

	free(keywords);
}

/********************************************************************/
/* Misc functions                                                   */
/********************************************************************/

_q_static
void qcache_load_arches(const char *overlay)
{
	FILE *fp;
	char *filename, *s;
	size_t buflen, linelen;
	char *buf;

	xasprintf(&filename, "%s/profiles/arch.list", overlay);
	fp = fopen(filename, "re");
	if (!fp)
		goto done;

	archlist_count = 0;
	arch_longest_len = 0;
	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) != -1) {
		rmspace_len(buf, linelen);

		if ((s = strchr(buf, '#')) != NULL)
			*s = '\0';
		if (buf[0] == '\0')
			continue;

		bool ok;
		arches = add_set_unique(buf, arches, &ok);
		if (ok) {
			++archlist_count;
			arch_longest_len = MAX(arch_longest_len, strlen(buf));
		}
	}
	free(buf);

	fclose(fp);
 done:
	free(filename);
}

/*
 * int qcache_free();
 *
 * Deallocate variables (archlist)
 */
_q_static
void qcache_free(void)
{
	free_sets(arches);
}

/********************************************************************/
/* main                                                             */
/********************************************************************/

int qcache_main(int argc, char **argv)
{
	int i, action = 0;

	while ((i = GETOPT_LONG(QCACHE, qcache, "")) != -1) {
		switch (i) {
			case 'p': qcache_matchpkg = optarg; break;
			case 'c': qcache_matchcat = optarg; break;
			case 'i':
			case 'd':
			case 't':
			case 's':
			case 'a':
			case 'n':
				if (action)
					qcache_usage(EXIT_FAILURE); /* trying to use more than 1 action */
				action = i;
				break;

			COMMON_GETOPTS_CASES(qcache)
		}
	}

	if (optind < argc)
		qcache_test_arch = decode_arch(argv[optind]);

	if ((qcache_test_arch == -1 && action != 's') || optind + 1 < argc)
		qcache_usage(EXIT_FAILURE);

	switch (action) {
		case 'i': return qcache_traverse(qcache_imlate);
		case 'd': return qcache_traverse(qcache_dropped);
		case 't': return qcache_traverse(qcache_testing_only);
		case 's': return qcache_traverse(qcache_stats);
		case 'a': return qcache_traverse(qcache_all);
		case 'n': return qcache_traverse(qcache_not);
	}

	qcache_free();
	qcache_usage(EXIT_FAILURE);
	return EXIT_FAILURE;
}

#else
DEFINE_APPLET_STUB(qcache)
#endif
