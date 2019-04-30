/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2006      Thomas A. Cort - <tcort@gentoo.org>
 * Copyright 2019-     Fabian Groffen - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <xalloc.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "atom.h"
#include "cache.h"
#include "scandirat.h"
#include "rmspace.h"
#include "set.h"
#include "xasprintf.h"

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

typedef struct {
	depend_atom *qatom;
	depend_atom *lastatom;
	int *keywordsbuf;
	size_t keywordsbuflen;
	const char *arch;
	cache_pkg_cb *runfunc;
} qcache_data;

static set *archs = NULL;
static char **archlist = NULL;
static size_t archlist_count;
static size_t arch_longest_len;
const char status[3] = {'-', '~', '+'};
int qcache_test_arch = 0;

enum { none = 0, testing, stable, minus };

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
static int
decode_status(char c)
{
	switch (c) {
		case '-': return minus;
		case '~': return testing;
		default:  return stable;
	}
}

/*
 * Decode the architecture string
 *
 * IN:
 *  const char *arch - name of an arch (alpha, amd64, ...)
 * OUT:
 *  int - position in keywords, or -1 if not found
 */
static int
decode_arch(const char *arch)
{
	char **q;
	int a;
	const char *p;

	p = arch;
	if (*p == '~' || *p == '-')
		p++;

	for (q = archlist, a = 0; *q != NULL; q++, a++) {
		if (strcmp(*q, p) == 0)
			return a;
	}

	return -1;
}

/*
 * Prints the keywords to stdout
 *
 * IN:
 *  char *category - current category of the current package
 *  int *keywords - an array of keywords that coincides with archlist
 */
static void
print_keywords(const char *category, const char *ebuild, int *keywords)
{
	char **arch = archlist;
	size_t a;

	printf("%s%s/%s%s%s ", BOLD, category, BLUE, ebuild, NORM);
	for (a = 0; a < archlist_count; a++) {
		switch (keywords[a]) {
			case stable:
				printf("%s%c%s%s ", GREEN, status[keywords[a]], arch[a], NORM);
				break;
			case testing:
				printf("%s%c%s%s ", YELLOW, status[keywords[a]], arch[a], NORM);
				break;
		}
	}

	printf("\n");
}

/*
 * Read the KEYWORDS string and decode the values
 *
 * IN:
 *  char *s - a keywords string (ex: "alpha ~amd64 -x86")
 *  int *keywords - the output
 * ERR:
 *  int rc - -1 is returned on error (if !s || !keywords)
 */
static int
read_keywords(char *s, int *keywords)
{
	char *arch, delim[2] = { ' ', '\0' };
	size_t slen;
	size_t a;
	int i;

	if (!s)
		return -1;

	memset(keywords, 0, sizeof(*keywords) * archlist_count);

	/* handle -* */
	slen = strlen(s);
	if (slen >= 2 && s[0] == '-' && s[1] == '*')
		for (a = 0; a < archlist_count; ++a)
			keywords[a] = minus;

	if (!slen)
		return 0;

	arch = strtok(s, delim);
	do {
		i = decode_arch(arch);
		if (i == -1)
			continue;
		keywords[i] = decode_status(arch[0]);
	} while ((arch = strtok(NULL, delim)));

	return 0;
}

/*
 * Compare 2 struct dirent d_name strings based with atom_compare_str().
 * Used with dirscan() to sort ebuild filenames by version, reversed.
 *
 * IN:
 *  2 (const struct dirent **) with d_name filled in
 * OUT:
 *  -1 (NEWER)
 *   1 (OLDER)
 *   0 (SAME)
 */
static int
qcache_vercmp(const struct dirent **x, const struct dirent **y)
{
	switch (atom_compare_str((*x)->d_name, (*y)->d_name)) {
		case EQUAL:      return  0;
		case NEWER:      return -1;
		case OLDER:      return  1;
		default:         return strcmp((*x)->d_name, (*y)->d_name);
	}
}

static int
qcache_imlate(cache_pkg_ctx *pkg_ctx, void *priv)
{
	size_t a;
	qcache_data *data = (qcache_data *)priv;

	switch (data->keywordsbuf[qcache_test_arch]) {
		case stable:
		case none:
		case minus:
			break;

		default:
			/* match if any of the other arches have stable keywords */
			for (a = 0; a < archlist_count; a++) {
				if (data->keywordsbuf[a] != stable)
					continue;
				print_keywords(pkg_ctx->cat_ctx->name, pkg_ctx->name,
						data->keywordsbuf);

				return EXIT_SUCCESS;
			}
	}

	return EXIT_FAILURE;
}

static int
qcache_not(cache_pkg_ctx *pkg_ctx, void *priv)
{
	size_t a;
	qcache_data *data = (qcache_data *)priv;

	if (data->keywordsbuf[qcache_test_arch] != testing &&
			data->keywordsbuf[qcache_test_arch] != stable)
	{
		/* match if any of the other arches have keywords */
		for (a = 0; a < archlist_count; a++) {
			if (data->keywordsbuf[a] == stable ||
					data->keywordsbuf[a] == testing)
				break;
		}
		if (a < archlist_count) {
			print_keywords(pkg_ctx->cat_ctx->name, pkg_ctx->name,
					data->keywordsbuf);
			return EXIT_SUCCESS;
		}
	}

	return EXIT_FAILURE;
}

static int
qcache_all(cache_pkg_ctx *pkg_ctx, void *priv)
{
	qcache_data *data = (qcache_data *)priv;

	if (data->keywordsbuf[qcache_test_arch] == stable ||
			data->keywordsbuf[qcache_test_arch] == testing)
	{
		print_keywords(pkg_ctx->cat_ctx->name, pkg_ctx->name,
				data->keywordsbuf);
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

static int
qcache_dropped(cache_pkg_ctx *pkg_ctx, void *priv)
{
	static bool candidate = false;
	static char pkg1[_Q_PATH_MAX];
	static char pkg2[_Q_PATH_MAX];
	static char *lastpkg = pkg1;
	static char *curpkg = pkg2;
	static char candpkg[_Q_PATH_MAX];
	static int *candkwds = NULL;
	static size_t candkwdslen = 0;

	qcache_data *data = (qcache_data *)priv;
	size_t i;
	char *p;

	/* a keyword is "dropped", if:
	 * - the keyword is present (stable or testing) in earlier ebuilds
	 * - there are other stable or testing keywords in the ebuild being
	 *   evaluated
	 * - the keyword is absent, thus not explicitly removed -keyword */

	/* mutt-1.10.4: amd64
	 * mutt-1.11.1: amd64 ppc64
	 * mutt-1.15.1: amd64           <-- this ebuild for ppc64
	 * mutt-9999:                                                    */

	p = lastpkg;
	lastpkg = curpkg;
	curpkg = p;
	if (pkg_ctx != NULL) {
		snprintf(curpkg, _Q_PATH_MAX, "%s/%s",
				pkg_ctx->cat_ctx->name, pkg_ctx->name);
	} else {
		curpkg[0] = '\0';
	}
	if (atom_compare_str(lastpkg, curpkg) == NOT_EQUAL)
	{
		/* different package, reset */
		candidate = false;
	}

	if (data == NULL) {
		if (candkwds != NULL)
			free(candkwds);
		return EXIT_SUCCESS;
	}

	if (candkwdslen < data->keywordsbuflen) {
		candkwds = xrealloc(candkwds,
				data->keywordsbuflen * sizeof(candkwds[0]));
		candkwdslen = data->keywordsbuflen;
	}

	/* explicitly removed? */
	if (data->keywordsbuf[qcache_test_arch] == minus)
		return EXIT_FAILURE;

	/* got a keyword? */
	if (data->keywordsbuf[qcache_test_arch] == testing ||
			data->keywordsbuf[qcache_test_arch] == stable)
	{
		if (candidate) {
			p = strchr(candpkg, '/');
			if (p != NULL) {
				*p++ = '\0';
				print_keywords(candpkg, p, candkwds);
			}
			candidate = false;
		}
		return EXIT_SUCCESS;  /* suppress further hits for this package */
	}

	/* do others have keywords? */
	for (i = 0; i < archlist_count; i++) {
		if (data->keywordsbuf[i] == stable || data->keywordsbuf[i] == testing) {
			/* we don't have a keyword, others do: candidate */
			break;
		}
	}
	if (i == archlist_count)
		return EXIT_FAILURE;

	/* keep the "highest" candidate */
	if (!candidate) {
		memcpy(candkwds, data->keywordsbuf,
				data->keywordsbuflen * sizeof(candkwds[0]));
		memcpy(candpkg, curpkg, _Q_PATH_MAX);
		candidate = true;
	}
	return EXIT_FAILURE;
}

static void
print_seconds_for_earthlings(const unsigned long t)
{
	unsigned dd, hh, mm, ss;
	unsigned long tt = t;
	ss = tt % 60; tt /= 60;
	mm = tt % 60; tt /= 60;
	hh = tt % 24; tt /= 24;
	dd = tt;
	if (dd)
		printf("%s%u%s day%s, ", GREEN, dd, NORM, (dd == 1 ? "" : "s"));
	if (hh)
		printf("%s%u%s hour%s, ", GREEN, hh, NORM, (hh == 1 ? "" : "s"));
	if (mm)
		printf("%s%u%s minute%s, ", GREEN, mm, NORM, (mm == 1 ? "" : "s"));
	printf("%s%u%s second%s", GREEN, ss, NORM, (ss == 1 ? "" : "s"));
}

static int
qcache_stats(cache_pkg_ctx *pkg_ctx, void *priv)
{
	static time_t runtime;
	static int numpkg  = 0;
	static int numebld = 0;
	static int numcat = 0;
	static int *packages_stable;
	static int *packages_testing;
	static int *current_package_keywords;
	static const char *lastcat = NULL;
	static char lastpkg[_Q_PATH_MAX];

	size_t a;
	depend_atom *atom;
	qcache_data *data = (qcache_data *)priv;

	/* Is this the last time we'll be called? */
	if (!data) {
		char **arch;
		const char border[] = "------------------------------------------------------------------";

		/* include stats for last package */
		for (a = 0; a < archlist_count; a++) {
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

		printf("+%.*s+\n", 25, border);
		printf("|   general statistics    |\n");
		printf("+%.*s+\n", 25, border);
		printf("| %s%13s%s | %s%7zd%s |\n",
				GREEN, "architectures", NORM, BLUE, archlist_count, NORM);
		printf("| %s%13s%s | %s%7d%s |\n",
				GREEN, "categories", NORM, BLUE, numcat, NORM);
		printf("| %s%13s%s | %s%7d%s |\n",
				GREEN, "packages", NORM, BLUE, numpkg, NORM);
		printf("| %s%13s%s | %s%7d%s |\n",
				GREEN, "ebuilds", NORM, BLUE, numebld, NORM);
		printf("+%.*s+\n\n", 25, border);

		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);
		printf("|%*skeyword distribution                          |\n",
			(int)arch_longest_len, "");
		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);
		printf("| %s%*s%s |%s%8s%s |%s%8s%s |%s%8s%s | %s%8s%s |\n",
			RED, (int)arch_longest_len, "architecture", NORM,
			RED, "stable", NORM,
			RED, "~arch", NORM, RED, "total", NORM, RED, "total/#pkgs", NORM);
		printf("| %*s |         |%s%8s%s |         |             |\n",
			(int)arch_longest_len, "", RED, "only", NORM);
		printf("+%.*s+\n", (int)(arch_longest_len + 46), border);

		arch = archlist;
		for (a = 0; a < archlist_count; a++) {
			printf("| %s%*s%s |", GREEN, (int)arch_longest_len, arch[a], NORM);
			printf("%s%8d%s |", BLUE, packages_stable[a], NORM);
			printf("%s%8d%s |", BLUE, packages_testing[a], NORM);
			printf("%s%8d%s |",
					BLUE, packages_testing[a] + packages_stable[a], NORM);
			printf("%s%11.2f%s%% |\n", BLUE,
					(100.0*(packages_testing[a]+packages_stable[a]))/numpkg,
					NORM);
		}

		printf("+%.*s+\n\n", (int)(arch_longest_len + 46), border);

		printf("Completed in ");
		print_seconds_for_earthlings(time(NULL) - runtime);
		printf("\n");

		free(packages_stable);
		free(packages_testing);
		free(current_package_keywords);
		return EXIT_SUCCESS;
	}

	if (numpkg == 0) {
		runtime                  = time(NULL);
		packages_stable          =
			xcalloc(archlist_count, sizeof(*packages_stable));
		packages_testing         =
			xcalloc(archlist_count, sizeof(*packages_testing));
		current_package_keywords =
			xcalloc(archlist_count, sizeof(*current_package_keywords));
	}

	if (lastcat != pkg_ctx->cat_ctx->name)
		numcat++;
	lastcat = pkg_ctx->cat_ctx->name;

	atom = atom_explode(pkg_ctx->name);
	if (atom && strcmp(lastpkg, atom->PN) != 0) {
		for (a = 0; a < archlist_count; a++) {
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

		numpkg++;
		snprintf(lastpkg, sizeof(lastpkg), "%s", atom->PN);
		memset(current_package_keywords, 0,
				archlist_count * sizeof(*current_package_keywords));
	}
	atom_implode(atom);

	numebld++;

	for (a = 0; a < archlist_count; a++) {
		switch (data->keywordsbuf[a]) {
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

	return EXIT_FAILURE;
}

static int
qcache_testing_only(cache_pkg_ctx *pkg_ctx, void *priv)
{
	static bool candidate = false;
	static char pkg1[_Q_PATH_MAX];
	static char pkg2[_Q_PATH_MAX];
	static char *lastpkg = pkg1;
	static char *curpkg = pkg2;
	static char candpkg[_Q_PATH_MAX];
	static int *candkwds = NULL;
	static size_t candkwdslen = 0;

	qcache_data *data = (qcache_data *)priv;
	char *p;

	p = lastpkg;
	lastpkg = curpkg;
	curpkg = p;
	if (pkg_ctx != NULL) {
		snprintf(curpkg, _Q_PATH_MAX, "%s/%s",
				pkg_ctx->cat_ctx->name, pkg_ctx->name);
	} else {
		curpkg[0] = '\0';
	}
	if (atom_compare_str(lastpkg, curpkg) == NOT_EQUAL)
	{
		/* different package, print if candidate */
		if (candidate) {
			p = strchr(candpkg, '/');
			if (p != NULL) {
				*p++ = '\0';
				print_keywords(candpkg, p, candkwds);
			}
			candidate = false;
		}
	}

	if (data == NULL) {
		if (candkwds != NULL)
			free(candkwds);
		return EXIT_SUCCESS;
	}

	if (candkwdslen < data->keywordsbuflen) {
		candkwds = xrealloc(candkwds,
				data->keywordsbuflen * sizeof(candkwds[0]));
		candkwdslen = data->keywordsbuflen;
	}

	/* explicitly removed or unkeyworded? */
	if (data->keywordsbuf[qcache_test_arch] == minus ||
			data->keywordsbuf[qcache_test_arch] == none)
		return EXIT_FAILURE;

	/* got a stable keyword? */
	if (data->keywordsbuf[qcache_test_arch] == stable)
		return EXIT_SUCCESS;  /* suppress further hits for this package */

	/* must be testing at this point */

	/* keep the "highest" candidate */
	if (!candidate) {
		memcpy(candkwds, data->keywordsbuf,
				data->keywordsbuflen * sizeof(candkwds[0]));
		memcpy(candpkg, curpkg, _Q_PATH_MAX);
		candidate = true;
	}
	return EXIT_FAILURE;
}

static int
qcache_results_cb(cache_pkg_ctx *pkg_ctx, void *priv)
{
	int *keywords;
	qcache_data *data = (qcache_data *)priv;
	char buf[_Q_PATH_MAX];
	depend_atom *patom = NULL;
	cache_pkg_meta *meta;
	int ret;

	snprintf(buf, sizeof(buf), "%s/%s",
			pkg_ctx->cat_ctx->name, pkg_ctx->name);
	patom = atom_explode(buf);
	if (patom == NULL)
		return EXIT_FAILURE;

	if (data->qatom != NULL &&
			atom_compare(patom, data->qatom) != EQUAL)
	{
		atom_implode(patom);
		return EXIT_FAILURE;
	}

	if (data->lastatom != NULL &&
			atom_compare(data->lastatom, patom) != NOT_EQUAL)
	{
		atom_implode(patom);
		return EXIT_SUCCESS;
	}

	keywords = data->keywordsbuf;
	meta = cache_pkg_read(pkg_ctx);
	if (meta == NULL) {
		atom_implode(patom);
		return EXIT_FAILURE;
	}

	if (read_keywords(meta->KEYWORDS, keywords) < 0) {
		if (verbose)
			warn("Failed to read keywords for %s%s/%s%s%s",
				BOLD, pkg_ctx->cat_ctx->name, BLUE, pkg_ctx->name, NORM);
		atom_implode(patom);
		return EXIT_FAILURE;
	}

	ret = data->runfunc(pkg_ctx, priv);

	if (ret == EXIT_SUCCESS) {
		/* store CAT/PN in lastatom */
		patom->P = patom->PN;
		patom->PVR = patom->PN;
		patom->PR_int = 0;
		data->lastatom = patom;
		patom = NULL;
	} else {
		atom_implode(patom);
	}

	return EXIT_SUCCESS;
}

static void
qcache_load_arches(const char *overlay)
{
	FILE *fp;
	char *filename, *s;
	int linelen;
	size_t buflen;
	char *buf;

	xasprintf(&filename, "%s/%s/profiles/arch.list", portroot, overlay);
	fp = fopen(filename, "re");
	if (!fp)
		goto done;

	clear_set(archs);
	archlist_count = 0;
	arch_longest_len = 0;
	buf = NULL;
	while ((linelen = getline(&buf, &buflen, fp)) >= 0) {
		rmspace_len(buf, (size_t)linelen);

		if ((s = strchr(buf, '#')) != NULL)
			*s = '\0';
		if (buf[0] == '\0')
			continue;

		bool ok;
		archs = add_set_unique(buf, archs, &ok);
		if (ok) {
			archlist_count++;
			buflen = strlen(buf);
			if (arch_longest_len < buflen)
				arch_longest_len = buflen;
		}
	}
	free(buf);

	/* materialise into a list */
	if (archlist != NULL)
		free(archlist);
	list_set(archs, &archlist);

	fclose(fp);
 done:
	free(filename);
}

static int
qcache_traverse(cache_pkg_cb func, void *priv)
{
	int ret;
	size_t n;
	const char *overlay;
	qcache_data *data = (qcache_data *)priv;

	/* Preload all the arches. Not entirely correctly (as arches are bound
	 * to overlays if set), but oh well. */
	array_for_each(overlays, n, overlay)
		qcache_load_arches(overlay);

	/* allocate memory (once) for the list used by various funcs */
	if (archlist_count > data->keywordsbuflen) {
		data->keywordsbuf = xrealloc(data->keywordsbuf,
				archlist_count * sizeof(data->keywordsbuf[0]));
		data->keywordsbuflen = archlist_count;
	}

	qcache_test_arch = decode_arch(data->arch);
	if (qcache_test_arch == -1)
		return EXIT_FAILURE;

	data->runfunc = func;
	ret = 0;
	array_for_each(overlays, n, overlay)
		ret |= cache_foreach_pkg_sorted(portroot, overlay,
				qcache_results_cb, priv, NULL, qcache_vercmp);

	return ret;
}

int qcache_main(int argc, char **argv)
{
	int i;
	char action = '\0';
	qcache_data data;
	char *pkg = NULL;
	char *cat = NULL;

	while ((i = GETOPT_LONG(QCACHE, qcache, "")) != -1) {
		switch (i) {
			case 'p': pkg = optarg; break;
			case 'c': cat = optarg; break;
			case 'i':
			case 'd':
			case 't':
			case 's':
			case 'a':
			case 'n':
				if (action)
					qcache_usage(EXIT_FAILURE);
					/* trying to use more than 1 action */
				action = i;
				break;

			COMMON_GETOPTS_CASES(qcache)
		}
	}

	data.arch = NULL;
	if (optind < argc)
		data.arch = argv[optind];

	if ((data.arch == NULL && action != 's') || optind + 1 < argc)
		qcache_usage(EXIT_FAILURE);

	if (cat != NULL) {
		char buf[_Q_PATH_MAX];

		snprintf(buf, sizeof(buf), "%s/%s", cat, pkg == NULL ? "" : pkg);
		data.qatom = atom_explode(buf);
		if (data.qatom == NULL) {
			warnf("invalid cat/pkg: %s\n", buf);
			return EXIT_FAILURE;
		}
	} else if (pkg != NULL) {
		data.qatom = atom_explode(pkg);
		if (data.qatom == NULL) {
			warnf("invalid pkg: %s\n", pkg);
			return EXIT_FAILURE;
		}
	} else {
		data.qatom = NULL;
	}

	archs = create_set();
	data.lastatom = NULL;
	data.keywordsbuf = NULL;
	data.keywordsbuflen = 0;

	switch (action) {
		case 'i': i = qcache_traverse(qcache_imlate, &data);          break;
		case 'd': i = qcache_traverse(qcache_dropped, &data);
				  i = qcache_dropped(NULL, NULL);                     break;
		case 't': i = qcache_traverse(qcache_testing_only, &data);
				  i = qcache_testing_only(NULL, NULL);                break;
		case 's': data.arch = "amd64";  /* doesn't matter, need to be set */
				  i = qcache_traverse(qcache_stats, &data);
				  i = qcache_stats(NULL, NULL);                       break;
		case 'a': i = qcache_traverse(qcache_all, &data);             break;
		case 'n': i = qcache_traverse(qcache_not, &data);             break;
		default:  i = -2;                                             break;
	}

	if (data.qatom != NULL)
		atom_implode(data.qatom);
	free(archlist);
	free_set(archs);
	if (i == -2)
		qcache_usage(EXIT_FAILURE);
	return i;
}
