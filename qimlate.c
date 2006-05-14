/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/Attic/qimlate.c,v 1.4 2006/05/14 00:17:14 tcort Exp $
 *
 * Copyright 2006 Thomas A. Cort  - <tcort@gentoo.org>
 */

#ifdef APPLET_qimlate

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/dir.h>
#include <sys/types.h>
#include <sys/stat.h>

#define QIMLATE_FLAGS "" COMMON_FLAGS
static struct option const qimlate_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qimlate_opts_help[] = {
	COMMON_OPTS_HELP
};

static const char qimlate_rcsid[] = "$Id: qimlate.c,v 1.4 2006/05/14 00:17:14 tcort Exp $";
#define qimlate_usage(ret) usage(ret, QIMLATE_FLAGS, qimlate_long_opts, qimlate_opts_help, lookup_applet_idx("qimlate"))

#define NUM_ARCHES (16)
enum { unknown = 0, alpha, amd64, arm, hppa, ia64, m68k, mips, ppc, ppc64, ppc_macos, s390, sh, sparc, x86, x86_fbsd };
enum { none = 0, testing, stable };

char  arches[NUM_ARCHES][10] = {"unknown", "alpha", "amd64", "arm", "hppa", "ia64", "m68k", "mips", "ppc", "ppc64", "ppc-macos", "s390", "sh", "sparc", "x86", "x86-fbsd" };
char  status[3]              = {'-','~','+'};

char *current_package;
char *current_category;

int decode_status(char c);
int decode_status(char c) {
	switch (c) {
		case '-': return none;
		case '~': return testing;
		default:  return stable;
	}
}

int decode_arch(char *arch);
int decode_arch(char *arch) {
	if (arch[0] == '~' || arch[0] == '-') arch += 1;

	switch (strlen(arch)) {
		case 3:
			if      (!strcmp(arch,"x86")) return x86;
			else if (!strcmp(arch,"ppc")) return ppc;
			else if (!strcmp(arch,"arm")) return arm;
			else return unknown;
		case 4:
			if      (!strcmp(arch,"mips")) return mips;
			else if (!strcmp(arch,"hppa")) return hppa;
			else if (!strcmp(arch,"ia64")) return ia64;
			else if (!strcmp(arch,"s390")) return s390;
			else if (!strcmp(arch,"m68k")) return m68k;
			else return unknown;
		case 5:
			if      (!strcmp(arch,"amd64")) return amd64;
			else if (!strcmp(arch,"ppc64")) return ppc64;
			else if (!strcmp(arch,"sparc")) return sparc;
			else if (!strcmp(arch,"alpha")) return alpha;
			else return unknown;
		case 8:
			if (!strcmp(arch,"x86-fbsd")) return x86_fbsd;
			else return unknown;
		case 9:
			if (!strcmp(arch,"ppc-macos")) return ppc_macos;
			else return unknown;
		case 2:
			if (!strcmp(arch,"sh")) return sh;
			else return unknown;
		default:
			return unknown;

	}
}

int read_keywords(char *pkg, int *keywords);
int read_keywords(char *pkg, int *keywords) {
	int fd, i;
	char c, arch[16], count = 0;

	memset(keywords,none,NUM_ARCHES*sizeof(int));

	if ((fd = open(pkg, O_RDONLY)) < 0) return fd;

	while (read(fd,&c,1)) {
		if (c == '\n' && ++count == 8) {
			do { /* read KEYWORDS line */
				for (i = 0; read(fd,&c,1) && c != ' ' && c != '\n' && i < 16; i++) arch[i] = c;
				arch[i] = '\0';
				keywords[decode_arch(arch)] = decode_status(arch[0]);
			} while (c != '\n');
			break;
		}
	}

	return close(fd);
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

int qimlate_main(int argc, char **argv)
{
	int i, j, k, l, m, numcat, numpkg, numebld, test_arch = 0, fnd, keywords[NUM_ARCHES];
	char *pathcat, *pathpkg, *pathebld, *pathcache;
	struct direct **categories, **packages, **ebuilds;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QIMLATE, qimlate, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qimlate)
		}
	}

	if (argc != optind + 1 || !(test_arch = decode_arch(argv[optind])))
		qimlate_usage(EXIT_FAILURE);

	pathcache = (char *) xmalloc(strlen(portdir) + strlen("/metadata/cache") + 1);
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
		if (numcat == (-1))
			err("%s %s", pathcat, strerror(errno));

		if (!numcat)
			warn("%s is empty!",pathcat);

		for (j = 0; j < numpkg; j++) {
			pathpkg = (char *) xmalloc(strlen(portdir) + 1 /* '/' */ + strlen(categories[i]->d_name) + 1 /* '/' */ + strlen(packages[j]->d_name) + 1 /* '\0' */);
			fnd = 0;

			strcpy(pathpkg,pathcat);
			strcat(pathpkg+strlen(pathcat),"/");
			strcat(pathpkg+strlen(pathcat)+strlen("/"),packages[j]->d_name);

			current_package = packages[j]->d_name;
			numebld = scandir(pathpkg, &ebuilds, ebuild_select, vercmp);
			if (numebld == (-1))
				err("%s %s", pathpkg, strerror(errno));
			if (!numebld)
				warn("%s is empty!",pathpkg);

			free(pathpkg);

			for (k = 0; k < numebld; k++) {
				pathebld = (char *) xmalloc(strlen(pathcache) + strlen(categories[i]->d_name) + 1 + strlen(ebuilds[k]->d_name) + 1);

				strcpy(pathebld,pathcache);
				strcat(pathebld+strlen(pathcache),categories[i]->d_name);
				strcat(pathebld+strlen(pathcache)+strlen(categories[i]->d_name),"/");
				strcat(pathebld+strlen(pathcache)+strlen(categories[i]->d_name)+1,ebuilds[k]->d_name);

				pathebld[strlen(pathebld)-7] = '\0';

				if (!fnd) {
					if (read_keywords(pathebld,keywords) < 0) {
						warn("Failed to read keywords for %s%s/%s%s%s",BOLD,current_category,BLUE,pathebld+strlen(pathcache)+strlen(categories[i]->d_name)+1,NORM);
					} else {
						switch (keywords[test_arch]) {
							case stable: fnd = 1; break;
							case none: break;
							default:
								for (l = 0; l < NUM_ARCHES && !fnd; l++) {
									if (keywords[l] == stable) {
										fnd = 1;
										printf("%s%s/%s%s%s ",BOLD,current_category,BLUE,pathebld+strlen(pathcache)+strlen(categories[i]->d_name)+1,NORM);
										for (m = 0; m < NUM_ARCHES; m++)
											switch (keywords[m]) {
												case stable: printf("%s%c%s%s ",GREEN,status[keywords[m]],arches[m],NORM); break;
												case testing: printf("%s%c%s%s ",YELLOW,status[keywords[m]],arches[m],NORM); break;
												default: break;
											}
										printf("\n");
									}
								}
						}

					}
				}

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

#else
DEFINE_APPLET_STUB(qimlate)
#endif
