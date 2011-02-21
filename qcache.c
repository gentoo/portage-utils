/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcache.c,v 1.39 2011/02/21 07:33:21 vapier Exp $
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

static const char qcache_rcsid[] = "$Id: qcache.c,v 1.39 2011/02/21 07:33:21 vapier Exp $";
#define qcache_usage(ret) usage(ret, QCACHE_FLAGS, qcache_long_opts, qcache_opts_help, lookup_applet_idx("qcache"))

/********************************************************************/
/* Constants                                                        */
/********************************************************************/

/* TODO: allow the user to override this value if s/he wishes */
#define QCACHE_EDB "/var/cache/edb/dep"

/********************************************************************/
/* Structs                                                          */
/********************************************************************/

typedef struct {
	char *category;
	char *package;
	char *ebuild;
	portage_cache *cache_data;
	unsigned char cur;
	unsigned char num;
} qcache_data;

/********************************************************************/
/* Global Variables                                                 */
/********************************************************************/

static char **archlist; /* Read from PORTDIR/profiles/arch.list in qcache_init() */
static unsigned int archlist_count;
const char status[3] = {'-', '~', '+'};
int qcache_skip, qcache_test_arch, qcache_last = 0;
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
int decode_status(char c);
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
unsigned int decode_arch(const char *arch);
unsigned int decode_arch(const char *arch)
{
	unsigned int a;
	const char *p;

	p = arch;
	if (*p == '~' || *p == '-')
		p++;

	for (a = 0; a < archlist_count; ++a)
		if (strcmp(archlist[a], p) == 0)
			return a;

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
void print_keywords(char *category, char *ebuild, int *keywords);
void print_keywords(char *category, char *ebuild, int *keywords)
{
	unsigned int a;
	char *package;

	package = xstrdup(ebuild);
	package[strlen(ebuild)-7] = '\0';

	printf("%s%s/%s%s%s ", BOLD, category, BLUE, package, NORM);
	for (a = 0; a < archlist_count; ++a) {
		switch (keywords[a]) {
			case stable:
				printf("%s%c%s%s ", GREEN, status[keywords[a]], archlist[a], NORM);
				break;
			case testing:
				printf("%s%c%s%s ", YELLOW, status[keywords[a]], archlist[a], NORM);
				break;
			default:
				break;
		}
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
int read_keywords(char *s, int *keywords);
int read_keywords(char *s, int *keywords)
{
	char *arch, delim[2] = { ' ', '\0' };
	size_t slen;
	unsigned int a;

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

/********************************************************************/
/* File reading helper functions                                    */
/********************************************************************/

/*
 * inline unsigned int qcache_count_lines(char *filename);
 *
 * Count the number of new line characters '\n' in a file.
 *
 * IN:
 *  char *filename - name of the file to read.
 * OUT:
 *  unsigned int count - number of new lines counted.
 * ERR:
 *  -1 is returned if the file cannot be read.
 */
static unsigned int qcache_count_lines(char *filename)
{
	unsigned int count, fd;
	char c;

	if ((fd = open(filename, O_RDONLY)) != -1) {
		count = 0;

		while (read(fd, &c, 1) == 1)
			if (c == '\n')
				count++;

		close(fd);
		return count;
	}

	return -1;
}

/*
 * char **qcache_read_lines(char *filename);
 *
 * Reads in every line contained in a file
 *
 * IN:
 *  char *filename - name of the file to read.
 * OUT:
 *  char **lines - number of new lines counted.
 * ERR:
 *  NULL is returned if an error occurs.
 */
char **qcache_read_lines(char *filename);
char **qcache_read_lines(char *filename)
{
	unsigned int len, fd, count, i, num_lines;
	char **lines, c;

	if (-1 == (num_lines = qcache_count_lines(filename)))
		return NULL;

	len   = sizeof(char*) * (num_lines + 1);
	lines = xzalloc(len);

	if ((fd = open(filename, O_RDONLY)) != -1) {
		for (i = 0; i < num_lines; i++) {
			count = 0;

			/* determine the space needed for storing the line */
			while (read(fd, &c, 1) == 1 && c != '\n')
				count++;
			lseek(fd, (lseek(fd, 0, SEEK_CUR) - count - 1), SEEK_SET);

			lines[i] = xzalloc(sizeof(char) * (count+1));

			/* copy the line into lines[i] */
			assert(read(fd, lines[i], count) == count);
			assert(read(fd, &c, 1) == 1);	/* skip '\n' */
		}

		close(fd);
		return lines;
	}

	return NULL;
}

/*
 * void qcache_free_lines(char **lines);
 *
 * free()'s memory allocated by qcache_read_lines
 */
void qcache_free_lines(char **lines);
void qcache_free_lines(char **lines)
{
	int i;

	for (i = 0; lines[i]; i++)
		free(lines[i]);

	free(lines);
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
portage_cache *qcache_read_cache_file(const char *filename);
portage_cache *qcache_read_cache_file(const char *filename)
{
	struct stat s;
	char *ptr, *buf;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len, buflen;

	if ((f = fopen(filename, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0) {
		fclose(f);
		goto err;
	}

	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);

	while (getline(&buf, &buflen, f) != -1) {
		if ((ptr = strrchr(buf, '\n')) != NULL)
			*ptr = 0;

		if ((strncmp(buf, "DEPEND=", 7)) == 0)
			ret->DEPEND = xstrdup(buf + 7);

		if ((strncmp(buf, "DESCRIPTION=", 12)) == 0)
			ret->DESCRIPTION = xstrdup(buf + 12);

		if ((strncmp(buf, "HOMEPAGE=", 9)) == 0)
			ret->HOMEPAGE = xstrdup(buf + 9);

		if ((strncmp(buf, "INHERITED=", 10)) == 0)
			ret->INHERITED = xstrdup(buf + 10);

		if ((strncmp(buf, "IUSE=", 4)) == 0)
			ret->IUSE = xstrdup(buf + 4);

		if ((strncmp(buf, "KEYWORDS=", 9)) == 0)
			ret->KEYWORDS = xstrdup(buf + 9);

		if ((strncmp(buf, "LICENSE=", 8)) == 0)
			ret->LICENSE = xstrdup(buf + 8);

		if ((strncmp(buf, "PDEPEND=", 8)) == 0)
			ret->PDEPEND = xstrdup(buf + 8);

		if ((strncmp(buf, "PROVIDE=", 8)) == 0)
			ret->PROVIDE = xstrdup(buf + 8);

		if ((strncmp(buf, "RDEPEND=", 8)) == 0)
			ret->RDEPEND = xstrdup(buf + 8);

		if ((strncmp(buf, "RESTRICT=", 9)) == 0)
			ret->RESTRICT = xstrdup(buf + 9);

		if ((strncmp(buf, "SLOT=", 5)) == 0)
			ret->SLOT = xstrdup(buf + 5);

		if ((strncmp(buf, "SRC_URI=", 8)) == 0)
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
void qcache_free_data(portage_cache *cache);
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
int qcache_vercmp(const struct dirent **x, const struct dirent **y);
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
int qcache_file_select(const struct dirent *entry);
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
int qcache_ebuild_select(const struct dirent *entry);
int qcache_ebuild_select(const struct dirent *entry)
{
	return ((strlen(entry->d_name) > 7) && !strcmp(entry->d_name+strlen(entry->d_name)-7, ".ebuild"));
}

/********************************************************************/
/* Traversal function                                               */
/********************************************************************/

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
int qcache_traverse(void (*func)(qcache_data*));
int qcache_traverse(void (*func)(qcache_data*))
{
	qcache_data data;
	char *catpath, *pkgpath, *ebuildpath, *cachepath;
	int i, j, k, len, num_cat, num_pkg, num_ebuild;
	struct dirent **categories, **packages, **ebuilds;

	xasprintf(&catpath, "%s%s", QCACHE_EDB, portdir);

	if (-1 == (num_cat = scandir(catpath, &categories, qcache_file_select, alphasort))) {
		errp("%s", catpath);
		free(catpath);
	}

	if (!num_cat)
		warn("%s is empty!", catpath);

	/* traverse categories */
	for (i = 0; i < num_cat; i++) {
		xasprintf(&pkgpath, "%s/%s", portdir, categories[i]->d_name);

		if (-1 == (num_pkg = scandir(pkgpath, &packages, qcache_file_select, alphasort))) {
			warnp("Found a cache dir, but unable to process %s", pkgpath);
			free(categories[i]);
			free(pkgpath);
			continue;
		}

		if (qcache_matchcat) {
			if (strcmp(categories[i]->d_name, qcache_matchcat) != 0) {
				for (j = 0; j < num_pkg; j++)
					free(packages[j]);
				free(categories[i]);
				free(packages);
				free(pkgpath);
				continue;
			}
		}

		/* traverse packages */
		for (j = 0; j < num_pkg; j++) {
			len = sizeof(char) * (strlen(portdir) + strlen("/") + strlen(categories[i]->d_name) + strlen("/") + strlen(packages[j]->d_name) + 1);
			ebuildpath = xzalloc(len);
			snprintf(ebuildpath, len, "%s/%s/%s", portdir, categories[i]->d_name, packages[j]->d_name);

			if (-1 == (num_ebuild = scandir(ebuildpath, &ebuilds, qcache_ebuild_select, qcache_vercmp))) {
				warnp("%s", ebuildpath);
				free(packages[i]);
				free(pkgpath);
				continue;
			}

			if (qcache_matchpkg) {
				if (strcmp(packages[j]->d_name, qcache_matchpkg) != 0) {
					for (k = 0; k < num_ebuild; k++)
						free(ebuilds[k]);
					free(packages[j]);
					free(ebuilds);
					free(ebuildpath);
					continue;
				}
			}

			qcache_skip = 0;

			/* traverse ebuilds */
			for (k = 0; k < num_ebuild; k++) {
				len = sizeof(char) * (strlen(catpath) + strlen("/") + strlen(categories[i]->d_name) + strlen("/") + strlen(ebuilds[k]->d_name) + 1);
				cachepath = xzalloc(len);
				snprintf(cachepath, len, "%s/%s/%s", catpath, categories[i]->d_name, ebuilds[k]->d_name);
				cachepath[len-8] = '\0'; /* remove ".ebuild" */

				data.category = categories[i]->d_name;
				data.package = packages[j]->d_name;
				data.ebuild = ebuilds[k]->d_name;
				data.cur = k + 1;
				data.num = num_ebuild;
				data.cache_data = qcache_read_cache_file(cachepath);

				if (data.cache_data != NULL) {
					/* is this the last ebuild? */
					if (i+1 == num_cat && j+1 == num_pkg && k+1 == num_ebuild)
						qcache_last = 1;

					if (!qcache_skip)
						func(&data);

					qcache_free_data(data.cache_data);
				} else
					warn("unable to read cache '%s'\n\tperhaps you need to `emerge --metadata` or `emerge --regen` ?", cachepath);

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

/********************************************************************/
/* functors                                                         */
/********************************************************************/

void qcache_imlate(qcache_data *data);
void qcache_imlate(qcache_data *data)
{
	int *keywords;
	unsigned int a;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
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

void qcache_not(qcache_data *data);
void qcache_not(qcache_data *data)
{
	int *keywords;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
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

void qcache_all(qcache_data *data);
void qcache_all(qcache_data *data)
{
	int *keywords;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
		return;
	}

	if (keywords[qcache_test_arch] == stable || keywords[qcache_test_arch] == testing) {
		qcache_skip = 1;
		printf("%s%s/%s%s%s\n", BOLD, data->category, BLUE, data->package, NORM);
	}

	free(keywords);
}

void qcache_dropped(qcache_data *data);
void qcache_dropped(qcache_data *data)
{
	static int possible = 0;
	int *keywords, i;

	if (data->cur == 1)
		possible = 0;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
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

void qcache_stats(qcache_data *data);
void qcache_stats(qcache_data *data)
{
	static time_t runtime;
	static unsigned int numpkg  = 0;
	static unsigned int numebld = 0;
	static unsigned int numcat;
	static unsigned int *packages_stable;
	static unsigned int *packages_testing;
	static int *current_package_keywords;
	static int *keywords;
	int i;
	unsigned int a;

	if (!numpkg) {
		struct dirent **categories;
		char *catpath;
		int len;

		len = sizeof(char) * (strlen(QCACHE_EDB) + strlen(portdir) + 1);
		catpath = xzalloc(len);
		snprintf(catpath, len, "%s%s", QCACHE_EDB, portdir);

		if (-1 == (numcat = scandir(catpath, &categories, qcache_file_select, alphasort))) {
			errp("%s", catpath);
			free(catpath);
		}

		for (i = 0; i < numcat; i++)
			free(categories[i]);
		free(categories);

		runtime = time(NULL);

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
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
		free(keywords);
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

	if (qcache_last) {
		printf("+-------------------------+\n");
		printf("|   general statistics    |\n");
		printf("+-------------------------+\n");
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "architectures", NORM, BLUE, archlist_count, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "categories", NORM, BLUE, numcat, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "packages", NORM, BLUE, numpkg, NORM);
		printf("| %s%13s%s | %s%7d%s |\n", GREEN, "ebuilds", NORM, BLUE, numebld, NORM);
		printf("+-------------------------+\n\n");

		printf("+----------------------------------------------------------+\n");
		printf("|                   keyword distribution                   |\n");
		printf("+----------------------------------------------------------+\n");
		printf("| %s%12s%s |%s%8s%s |%s%8s%s |%s%8s%s | %s%8s%s |\n", RED, "architecture", NORM, RED, "stable", NORM, RED, "~arch", NORM, RED, "total", NORM, RED, "total/#pkgs", NORM);
		printf("|              |         |%s%8s%s |         |             |\n", RED, "only", NORM);
		printf("+----------------------------------------------------------+\n");

		for (a = 0; a < archlist_count; ++a) {
			printf("| %s%12s%s |", GREEN, archlist[a], NORM);
			printf("%s%8d%s |", BLUE, packages_stable[a], NORM);
			printf("%s%8d%s |", BLUE, packages_testing[a], NORM);
			printf("%s%8d%s |", BLUE, packages_testing[a]+packages_stable[a], NORM);
			printf("%s%11.2f%s%% |\n", BLUE, (100.0*(packages_testing[a]+packages_stable[a]))/numpkg, NORM);
		}

		printf("+----------------------------------------------------------+\n\n");

		printf("Completed in %s%d%s seconds.\n", BLUE, (int)(time(NULL)-runtime), NORM);

		free(packages_stable);
		free(packages_testing);
		free(keywords);
		free(current_package_keywords);
	}
}

void qcache_testing_only(qcache_data *data);
void qcache_testing_only(qcache_data *data)
{
	static int possible = 0;
	int *keywords;

	if (data->cur == 1)
		possible = 0;

	keywords = xmalloc(sizeof(*keywords) * archlist_count);

	if (read_keywords(data->cache_data->KEYWORDS, keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s", BOLD, data->category, BLUE, data->ebuild, NORM);
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

/*
 * int qcache_init();
 *
 * Initialize variables (archlist, num_arches)
 *
 * OUT:
 *  0 is return on success.
 * ERR:
 *  -1 is returned on error.
 */
int qcache_init();
int qcache_init()
{
	char *filename;
	unsigned int len;

	len      = sizeof(char) * (strlen(portdir) + strlen("/profiles/arch.list") + 1);
	filename = xzalloc(len);

	snprintf(filename, len, "%s/profiles/arch.list", portdir);

	if (NULL == (archlist = qcache_read_lines(filename))) {
		free(filename);
		return -1;
	}

	len = 0;
	while (archlist[len])
		++len;
	archlist_count = len;

	free(filename);
	return 0;
}

/*
 * int qcache_free();
 *
 * Deallocate variables (archlist)
 */
void qcache_free();
void qcache_free()
{
	qcache_free_lines(archlist);
}

/********************************************************************/
/* main                                                             */
/********************************************************************/

int qcache_main(int argc, char **argv)
{
	int i, action = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
		argc, argv[0], argc > 1 ? argv[1] : "NULL?");

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

	if (-1 == qcache_init())
		err("Could not initialize arch list");

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
