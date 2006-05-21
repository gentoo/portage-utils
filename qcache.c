/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcache.c,v 1.2 2006/05/21 22:44:54 tcort Exp $
 *
 * Copyright 2006 Thomas A. Cort - <tcort@gentoo.org>
 */

#ifdef APPLET_qcache

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/types.h>
#include <sys/stat.h>

#define QCACHE_FLAGS "m:idta" COMMON_FLAGS
static struct option const qcache_long_opts[] = {
	{"match",    a_argument, NULL, 'm'},
	{"imlate",  no_argument, NULL, 'i'},
	{"dropped", no_argument, NULL, 'd'},
	{"testing", no_argument, NULL, 't'},
	{"all",     no_argument, NULL, 'a'},
	COMMON_LONG_OPTS
};

static const char *qcache_opts_help[] = {
	"match pkgname",
	"list packages that can be marked stable",
	"list packages that have dropped keywords on a version bump",
	"list packages that have ~arch versions, but no stable versions",
	"list all packages that have at least one version keyworded for an arch",
	COMMON_OPTS_HELP
};

static const char qcache_rcsid[] = "$Id: qcache.c,v 1.2 2006/05/21 22:44:54 tcort Exp $";
#define qcache_usage(ret) usage(ret, QCACHE_FLAGS, qcache_long_opts, qcache_opts_help, lookup_applet_idx("qcache"))

enum { none = 0, testing, stable };
char status[3] = {'-','~','+'};
char *current_package;
char *current_category;
char *qcache_match = NULL;
int qcache_skip, test_arch;

struct arch_list_t {
	const char *name;
} archlist[] = { 
	{ "unknown" },
	{ "alpha" },
	{ "amd64" },
	{ "arm" },
	{ "hppa" },
	{ "ia64" },
	{ "m68k" },
	{ "mips" },
	{ "ppc" },
	{ "ppc64" }, 
	{ "ppc-macos" },
	{ "s390" },
	{ "sh" },
	{ "sparc" },
	{ "x86" },
	{ "x86-fbsd" }
};

#define NUM_ARCHES ARRAY_SIZE(archlist)

int decode_status(char c);
int decode_status(char c) {
	switch (c) {
		case '-': return none;
		case '~': return testing;
		default:  return stable;
	}
}

int decode_arch(const char *arch);
int decode_arch(const char *arch) {
	int i;
	char *p = (char *) arch;

	if (*p == '~' || *p == '-')
		p++;

	for (i = 0 ; i < NUM_ARCHES; i++)
		if (strcmp(archlist[i].name, p) == 0) 
			return i;
	return 0;
}

int read_keywords(char *pkg, int *keywords);
int read_keywords(char *pkg, int *keywords) {
	int fd, i;
	char c, arch[NUM_ARCHES], count = 0;

	memset(keywords,none,NUM_ARCHES*sizeof(int));

	if ((fd = open(pkg, O_RDONLY)) < 0) return fd;

	while (read(fd,&c,1)) {
		if (c == '\n' && ++count == 8) {
			do { /* read KEYWORDS line */
				for (i = 0; read(fd,&c,1) && c != ' ' && c != '\n' && i < NUM_ARCHES; i++) arch[i] = c;
				arch[i] = '\0';
				keywords[decode_arch(arch)] = decode_status(arch[0]);
			} while (c != '\n');
			break;
		}
	}

	return close(fd);
}

void print_keywords(char *category, char *ebuild, int *keywords);
void print_keywords(char *category, char *ebuild, int *keywords) {
	int i;

	printf("%s%s/%s%s%s ",BOLD,category,BLUE,ebuild,NORM);
	for (i = 0; i < NUM_ARCHES; i++) {
		switch (keywords[i]) {
			case stable: printf("%s%c%s%s ",GREEN,status[keywords[i]],archlist[i].name,NORM); break;
			case testing: printf("%s%c%s%s ",YELLOW,status[keywords[i]],archlist[i].name,NORM); break;
			default: break;
		}
	}
	printf("\n");
}

int file_select(const struct dirent *entry);
int file_select(const struct dirent *entry) {
	return !(entry->d_name[0] == '.' || (strcmp(entry->d_name, "metadata.xml") == 0));
}

int ebuild_select(const struct dirent *entry);
int ebuild_select(const struct dirent *entry) {
	return (strlen(current_package) < strlen(entry->d_name) && 
		(strlen(entry->d_name) > 7) && !strcmp(entry->d_name+strlen(entry->d_name)-7,".ebuild") &&
		!strncmp(entry->d_name, current_package, strlen(current_package)));
}

int vercmp(const void *x, const void *y);
int vercmp(const void *x, const void *y) {
	depend_atom *a1, *a2;
	int cclen = strlen(current_category), rc;

	char *s1 = (char *) xmalloc(strlen((*((const struct dirent **)x))->d_name) + cclen + 2);
	char *s2 = (char *) xmalloc(strlen((*((const struct dirent **)y))->d_name) + cclen + 2);

	strcpy(s1,current_category);
	strcpy(s2,current_category);

	strcat(s1+cclen,"/");
	strcat(s2+cclen,"/");

	strcat(s1+cclen+1, (*((const struct dirent **)x))->d_name);
	strcat(s2+cclen+1, (*((const struct dirent **)y))->d_name);

	/* remove '.ebuild' */
	s1[strlen(s1)-7] = '\0';
	s2[strlen(s2)-7] = '\0';

	a1 = atom_explode(s1);
	a2 = atom_explode(s2);

	rc = atom_compare(a1, a2);

	free(s1); free(s2);
	free(a1); free(a2);

	switch (rc) {
		case NEWER: return -1;
		case OLDER: return  1;
		default:    return  0;
	}
}

/*
 * traverse_metadata_cache
 *
 * Traverses ${PORTDIR}/metadata/cache and calls func for every ebuild entry.
 * Goes to each category in alphabetical order, then to each package in
 * alphabetical order, and then to each cache file in version order from latest
 * to oldest.
 *
 * func(char *path, char *category, char *ebuild, int current, int num);
 *   path - path to metadata cache item (ex: "/usr/portage/metadata/cache/app-portage/portage-utils-0.1.17")
 *   category - category of the package (ex: "app-portage")
 *   ebuild - metadata cache item (ex: "portage-utils-0.1.17")
 *   current and num - num is the number of versions available. current is the index of the current version. (1st index is 1)
 *
 * *skip - for every package this value is reset to 0. Set this value to one if you want to skip the rest of the metadata 
 *         cache items for the current package.
 */
int traverse_metadata_cache(void (*func)(char*,char*,char*,int,int), int *skip);
int traverse_metadata_cache(void (*func)(char*,char*,char*,int,int), int *skip) {
	int i, j, k, numcat, numpkg, numebld;
	char *pathcat, *pathpkg, *pathebld, *pathcache, *ebuild, *category;
	struct direct **categories, **packages, **ebuilds;

	pathcache = (char *) xmalloc(strlen(portdir) + strlen("/metadata/cache/") + 1);
	strcpy(pathcache,portdir);
	strcat(pathcache+strlen(portdir),"/metadata/cache/");

	numcat = scandir(pathcache, &categories, file_select, alphasort);
	if (numcat == (-1))
		err("%s %s", pathcache, strerror(errno));

	if (!numcat)
		warn("%s is empty!", pathcache);

	for (i = 0; i < numcat; i++) {

		pathcat = (char *) xmalloc(strlen(portdir) + 1 /* '/' */ + strlen(categories[i]->d_name) + 1 /* '\0' */);
		strcpy(pathcat,portdir);
		strcat(pathcat+strlen(portdir), "/");
		strcat(pathcat+strlen(portdir) + 1, categories[i]->d_name);

		current_category = categories[i]->d_name;

		numpkg = scandir(pathcat, &packages, file_select, alphasort);
		if (numpkg == (-1)) {
			warn("%s %s", pathcat, strerror(errno));
			free(pathcat);
			continue;
		}

		if (!numpkg && verbose)
			warn("%s is empty!",pathcat);

		for (j = 0; j < numpkg; j++) {
			pathpkg = (char *) xmalloc(strlen(portdir) + 1 /* '/' */ + strlen(categories[i]->d_name) + 1 /* '/' */ + strlen(packages[j]->d_name) + 1 /* '\0' */);
			*skip = 0;

			strcpy(pathpkg,pathcat);
			strcat(pathpkg+strlen(pathcat),"/");
			strcat(pathpkg+strlen(pathcat)+strlen("/"),packages[j]->d_name);

			current_package = packages[j]->d_name;

			if (qcache_match) {
				if (strcmp(current_package, qcache_match) != 0) {
					free(pathpkg);
					continue;
				}
			}
			numebld = scandir(pathpkg, &ebuilds, ebuild_select, vercmp);
			if (numebld == (-1)) {
				free(pathpkg);
				continue;
			}
			if (!numebld && verbose)
				warn("%s is empty!",pathpkg);

			free(pathpkg);

			for (k = 0; k < numebld; k++) {
				if ((*skip)) {
					free(ebuilds[k]);
					continue;
				}
				pathebld = (char *) xmalloc(strlen(pathcache) + strlen(categories[i]->d_name) + 1 + strlen(ebuilds[k]->d_name) + 1);
				category = (char *) xmalloc(strlen(categories[i]->d_name) + 1);
				ebuild   = (char *) xmalloc(strlen(ebuilds[k]->d_name) + 1);
				memset(ebuild,0,strlen(ebuilds[k]->d_name+1));

				strcpy(pathebld,pathcache);
				strcat(pathebld+strlen(pathcache),categories[i]->d_name);
				strcat(pathebld+strlen(pathcache)+strlen(categories[i]->d_name),"/");
				strcat(pathebld+strlen(pathcache)+strlen(categories[i]->d_name)+1,ebuilds[k]->d_name);
				pathebld[strlen(pathebld)-7] = '\0';

				strncpy(ebuild,ebuilds[k]->d_name,strlen(ebuilds[k]->d_name)-7);
				strcpy(category,categories[i]->d_name);

				if (!(*skip))
					func(pathebld,category,ebuild,k+1,numebld);

				free(ebuild);
				free(category);
				free(pathebld);
				free(ebuilds[k]);
			}
			free(ebuilds);
			free(packages[j]);
		}
		free(packages);
		free(pathcat);
		free(categories[i]);
	}
	free(pathcache);
	free(categories);

	return EXIT_SUCCESS;
}

void qcache_imlate(char *path, char *category, char *ebuild, int current, int num);
void qcache_imlate(char *path, char *category, char *ebuild, int current, int num) {
	int keywords[NUM_ARCHES], i;

	if (read_keywords(path,keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s",BOLD,category,BLUE,ebuild,NORM);
		return;
	}
	switch (keywords[test_arch]) {
		case stable: qcache_skip = 1; break;
		case none: break;
		default:
			for (i = 0; i < NUM_ARCHES && !(qcache_skip); i++) {
				if (keywords[i] != stable)
					continue;
				qcache_skip = 1;
				print_keywords(category,ebuild,keywords);
			}
	}
}

void qcache_dropped(char *path, char *category, char *ebuild, int current, int num);
void qcache_dropped(char *path, char *category, char *ebuild, int current, int num) {
	int keywords[NUM_ARCHES], i;
	static int possible = 0;

	if (current == 1) possible = 0;

	if (read_keywords(path,keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s",BOLD,category,BLUE,ebuild,NORM);
		return;
	}
	if (keywords[test_arch] != none) {
		qcache_skip = 1;

		if (possible) {
			char *temp = ebuild;
			qcache_skip = 1;

			do {
				temp = strchr((temp),'-') + 1;
			} while (!isdigit(*temp));
			*(temp-1) = '\0';

			printf("%s%s/%s%s%s\n",BOLD,category,BLUE,ebuild,NORM);
		}
		return;
	}
	for (i = 0; i < NUM_ARCHES; i++) {
		if (keywords[i] != none) {
			possible = 1;
		}
	}
}

void qcache_testing_only(char *path, char *category, char *ebuild, int current, int num);
void qcache_testing_only(char *path, char *category, char *ebuild, int current, int num) {
	int keywords[NUM_ARCHES];
	static int possible = 0;

	if (current == 1) possible = 0;

	if (read_keywords(path,keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s",BOLD,category,BLUE,ebuild,NORM);
		return;
	}
	if (keywords[test_arch] == stable) {
		qcache_skip = 1;
		return;
	}
	if (keywords[test_arch] == testing) possible = 1;

	if (current == num && possible) {
		char *temp = ebuild;
		qcache_skip = 1;

		do {
			temp = strchr((temp),'-') + 1;
		} while (!isdigit(*temp));
		*(temp-1) = '\0';

		printf("%s%s/%s%s%s\n",BOLD,category,BLUE,ebuild,NORM);
	}
}

void qcache_all(char *path, char *category, char *ebuild, int current, int num);
void qcache_all(char *path, char *category, char *ebuild, int current, int num) {
	int keywords[NUM_ARCHES];

	if (read_keywords(path,keywords) < 0) {
		warn("Failed to read keywords for %s%s/%s%s%s",BOLD,category,BLUE,ebuild,NORM);
		return;
	}
	if (keywords[test_arch] != none) {
		char *temp = ebuild;
		qcache_skip = 1;

		do {
			temp = strchr((temp),'-') + 1;
		} while (!isdigit(*temp));
		*(temp-1) = '\0';

		printf("%s%s/%s%s%s\n",BOLD,category,BLUE,ebuild,NORM);
	}
}

int qcache_main(int argc, char **argv) {
	int i, action = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCACHE, qcache, "")) != -1) {
		switch (i) {
		case 'm': qcache_match = optarg; break;
		case 'i':
		case 'd':
		case 't':
		case 'a':
			if (action)
				qcache_usage(EXIT_FAILURE); /* trying to use more than 1 action */
			action = i;
			break;
		COMMON_GETOPTS_CASES(qcache)
		}
	}

	if (optind < argc)
		test_arch = decode_arch(argv[optind]);

	if (!test_arch)
		qcache_usage(EXIT_FAILURE);

	switch (action) {
		case 'i': return traverse_metadata_cache(qcache_imlate,&qcache_skip);
		case 'd': return traverse_metadata_cache(qcache_dropped,&qcache_skip);
		case 't': return traverse_metadata_cache(qcache_testing_only,&qcache_skip);
		case 'a': return traverse_metadata_cache(qcache_all,&qcache_skip);
	}

	qcache_usage(EXIT_FAILURE);
	return EXIT_FAILURE;
}

#else
DEFINE_APPLET_STUB(qcache)
#endif
