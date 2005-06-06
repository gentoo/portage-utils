/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.9 2005/06/06 23:39:22 vapier Exp $
 *
 * 2005 Ned Ludd <solar@gentoo.org>
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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>

typedef int (*APPLET)(int, char **);

APPLET lookup_applet(char *);

int rematch(char *, char *, int);
char *rmspace(char *);
void qfile(char *, char *);

char *initialize_portdir(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);

/* applet prototypes */
int q_main(int, char **);
int qsearch_main(int, char **);
int quse_main(int, char **);
int qlist_main(int, char **);
int qfile_main(int, char **);

/* helper functions for showing errors */
static char *argv0;
#define warn(fmt, args...) \
	fprintf(stderr, "%s: " fmt "\n", argv0, ## args)
#define warnf(fmt, args...) warn("%s(): " fmt, __FUNCTION__, ## args)
#define err(fmt, args...) \
	do { \
	warn(fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#ifdef EBUG
# define DBG(fmt, args...) warnf(fmt, ## args)
#else
# define DBG(fmt, args...)
#endif

static const char *rcsid = "$Id: q.c,v 1.9 2005/06/06 23:39:22 vapier Exp $";

static char color = 1;
static char exact = 0;
static int found = 0;
static char reinitialize = 0;

static char portdir[_POSIX_PATH_MAX] = "/usr/portage";

/* applets we support */
struct applet_t {
	const char *name;
	/* int *func; */
	APPLET func;
	const char *opts;
} applets[] = {
	/* q must always be the first applet */
	{"q",       (APPLET)q_main,       "<applet> <args>"},
	{"qfile",   (APPLET)qfile_main,   "<filename>"},
	{"qlist",   (APPLET)qlist_main,   "<pkgname>"},
	{"qsearch", (APPLET)qsearch_main, "<regex>"},
	{"quse",    (APPLET)quse_main,    "<useflag>"},
	{NULL,      (APPLET)NULL,         NULL}
};
#define Q_IDX 0
#define QFILE_IDX 1
#define QLIST_IDX 2
#define QSEARCH_IDX 3
#define QUSE_IDX 4

/* Common options for all applets */
#define COMMON_FLAGS "hV"
#define a_argument required_argument
#define COMMON_LONG_OPTS \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Print this help and exit", \
	"Print version and exit", \
	NULL

/* display usage and exit */
static void usage(int status, const char *flags, struct option const opts[], 
                  const char *help[], int applets_blabber)
{
	unsigned long i;
	if (applets_blabber == 0) {
		printf("Usage: q <applet> [arguments]...\n"
		       "   or: <applet> [arguments]...\n\n");
		printf("Currently defined applets:\n");
		for (i = 0; applets[i].name; ++i)
			printf(" - %s %s\n", applets[i].name, applets[i].opts);
	} else {
		printf("Usage: %s %s\n", applets[applets_blabber].name, applets[applets_blabber].opts);
	}

	printf("\nOptions: [%s]\n", flags);
	for (i = 0; opts[i].name; ++i)
		if (opts[i].has_arg == no_argument)
			printf("  -%c, --%-13s* %s\n", opts[i].val, 
			       opts[i].name, help[i]);
		else
			printf("  -%c, --%-6s <arg> * %s\n", opts[i].val,
			       opts[i].name, help[i]);
	exit(status);
}
static void version_barf(void)
{
	printf("%s compiled %s\n%s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       __FILE__, __DATE__, rcsid, argv0);
	exit(EXIT_SUCCESS);
}

APPLET lookup_applet(char *applet)
{
	unsigned int i;
	for (i = 0; applets[i].name; ++i) {
		if ((strcmp(applets[i].name, applet)) == 0) {
			DBG("found applet %s at %p", applets[i].name, applets[i].func);
			return applets[i].func;
		}
	}
	/* No applet found? Search by shortname then... */
	if ((strlen(applet)) - 1 > 0) {
		DBG("Looking up applet (%s) by short name", applet);
		for (i = 1; applets[i].name; ++i) {
			if ((strcmp(applets[i].name + 1, applet)) == 0) {
				DBG("found applet by short name %s", applets[i].name);
				return applets[i].func;
			}
		}
	}
	/* still nothing? .. add short opts -q/-l etc.. */
	warn("Unknown applet '%s'", applet);
	return 0;
}

int rematch(char *regex, char *match, int cflags)
{
	regex_t preg;
	int ret;

	ret = regcomp(&preg, regex, cflags);
	if (ret) {
		char err[256];
		if (regerror(ret, &preg, err, sizeof(err)))
			warnf("regcomp failed: %s", err);
		else
			warnf("regcomp failed");
		return EXIT_FAILURE;
	}
	ret = regexec(&preg, match, 0, NULL, 0);
	regfree(&preg);

	return ret;
}

char *rmspace(char *s)
{
	register char *p;
	/* wipe end of string */
	for (p = s + strlen(s) - 1; ((isspace(*p)) && (p >= s)); p--);
	if (p != s + strlen(s) - 1)
		*(p + 1) = 0;
	for (p = s; ((isspace(*p)) && (*p)); p++);
	if (p != s)
		strcpy(s, p);
	return (char *) s;
}

char *initialize_portdir(void)
{
	FILE *fp;
	char buf[_POSIX_PATH_MAX + 8];
	char *p = getenv("PORTDIR");
	size_t i;

	if (p) {
		if (strlen(p) < sizeof(portdir)) {
			strncpy(portdir, p, sizeof(portdir));
			return portdir;
		}
	}
	if ((fp = fopen("/etc/make.conf", "r")) != NULL) {
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			if (*buf != 'P')
				continue;
			if (strncmp(buf, "PORTDIR=", 8) != 0)
				continue;
			/* Sorry don't understand bash variables. */
			if ((strchr(buf, '$')) != NULL)
				continue;

			for (i = 8; i < strlen(buf); i++)
				if ((buf[i] == '"') || (buf[i] == '\''))
					buf[i] = ' ';

			rmspace(&buf[8]);
			strncpy(portdir, buf + 8, sizeof(portdir));
		}
		fclose(fp);
	}
	return portdir;
}

void qfile(char *path, char *fname)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *p, *ptr;
	size_t flen = strlen(fname);
	int base = 0;
	char buf[1024];

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return;

	if (!strchr(fname, '/'))
		base = 1;
	else
		base = 0;

	while ((dentry = readdir(dir))) {
		if (*dentry->d_name == '.')
			continue;
		if ((asprintf(&p, "%s/%s/CONTENTS", path, dentry->d_name)) == (-1))
			continue;
		if ((fp = fopen(p, "r")) == NULL) {
			free(p);
			continue;
		}
		free(p);
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			/* FIXME: Need to port portage_versions.py to c to do this properly. */
			if ((p = strchr(buf, ' ')) == NULL)
				continue;
			*p++;
			ptr = strdup(p);
			if (!ptr)
				continue;
			if ((p = strchr(ptr, ' ')) != NULL)
				*p++ = 0;
			if (strncmp(base ? basename(ptr) : ptr, fname, flen) != 0
			    || strlen(base ? basename(ptr) : ptr) != flen) {
				free(ptr);
				continue;
			}
			/* If the d_name is something like foo-3DVooDoo-0.0-r0 doing non
			 * exact matches would fail to display the name properly.
			 * /var/cvsroot/gentoo-x86/app-dicts/canna-2ch/
			 * /var/cvsroot/gentoo-x86/net-dialup/intel-536ep/
			 * /var/cvsroot/gentoo-x86/net-misc/cisco-vpnclient-3des/
			 * /var/cvsroot/gentoo-x86/sys-cluster/openmosix-3dmon-stats/
			 * /var/cvsroot/gentoo-x86/sys-cluster/openmosix-3dmon/
			 */
			if (!exact && (p = strchr(dentry->d_name, '-')) != NULL) {
				++p;
				if (*p >= '0' && *p <= '9') {
					--p;
					*p = 0;
				} else {
					/* tricky tricky.. I wish to advance to the second - */
					/* and repeat the first p strchr matching */
					char *q = strdup(p);
					if (!q) {
						free(ptr);
						continue;
					}
					if ((p = strchr(q, '-')) != NULL) {
						int l = 0;
						++p;
						if (*p >= '0' && *p <= '9') {
							--p;
							*p = 0;
							++p;
							l = strlen(dentry->d_name) - strlen(p) - 1;
							dentry->d_name[l] = 0;
						}
					}
					free(q);
				}
			}

			if (color)
				printf("\e[0;01m%s/\e[36;01m%s\e[0m (%s)\n", basename(path),
				       dentry->d_name, ptr);
			else
				printf("%s/%s (%s)\n", basename(path), dentry->d_name, ptr);

			free(ptr);
			fclose(fp);
			closedir(dir);
			found++;
			return;
		}
		fclose(fp);
	}
	closedir(dir);
	return;
}

/* 
 * Taken from emerge manpage.
 * --searchdesc (-S)
 *	Matches  the  search  string  against the description field as well as the package name.
 *	Take caution as the descriptions are also matched as regular expressions.
 */

/*
 * --search (-s)
 * Searches for matches of the supplied string in the portage tree.  The
 * --search string is a regular expression.  For example,
 * emerge --search "^kde" searches for any package that starts with
 * "kde"; emerge --search "gcc$" searches for any package that ends with
 * "gcc"; emerge --search "office" searches for any package that
 * contains the word "office".  If you want to search the package
 * descriptions as well, use the --searchdesc option.
 */

#define QSEARCH_FLAGS "s" COMMON_FLAGS
static struct option const qsearch_long_opts[] = {
	{"search",     no_argument, NULL, 's'},
	{"searchdesc",  a_argument, NULL, 'S'},
	COMMON_LONG_OPTS
};
static const char *qsearch_opts_help[] = {
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
	char *p, *str, *q;
	char *search_me;
	char search_desc = 1;
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

		case 's':
			search_desc = 0;
			break;
		case 'S':
			search_desc = 1;
		}
	}
	if (argc == optind)
		qsearch_usage(EXIT_FAILURE);
	search_me = argv[optind];

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

int quse_main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
	fp = fopen(".ebuild.x", "r");
	if (!fp)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			while ((fgets(buf, sizeof(buf), newfp)) != NULL) {
				if ((strncmp(buf, "IUSE=", 5)) == 0) {
					if ((p = strrchr(&buf[6], '"')) != NULL)
						*p = 0;
					if (argc > 1) {
						int i, ok = 0;
						for (i = 1; i < argc; i++) {
							if ((rematch(argv[i], &buf[6], REG_NOSUB)) != 0) {
								ok = 1;
								break;
							}
						}
						if (ok == 0)
							fprintf(stdout, "%s %s\n", ebuild, &buf[6]);
						} else {
							fprintf(stdout, "%s %s\n", ebuild, &buf[6]);
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
	fclose(fp);
	return 0;
}

#define QFILE_FLAGS "C" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	{"nocolor",     no_argument, NULL, 'C'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	"Don't ouput color",
	COMMON_OPTS_HELP
};
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, QFILE_IDX)

int qfile_main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *dentry;
	int i;
	char *p;
	const char *path = "/var/db/pkg";

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i=getopt_long(argc, argv, QFILE_FLAGS, qfile_long_opts, NULL)) != -1) {
		switch (i) {
		case 'V': version_barf(); break;
		case 'h': qfile_usage(EXIT_SUCCESS); break;
		}
	}
	if (argc == optind)
		qfile_usage(EXIT_FAILURE);

	if ((chdir(path) == 0) && ((dir = opendir(path)))) {
		while ((dentry = readdir(dir))) {
			if (*dentry->d_name == '.')
				continue;
			for (i = 1; i < argc; i++) {
				if (argv[i][0] != '-') {
					if ((asprintf(&p, "%s/%s", path, dentry->d_name)) != (-1)) {
						qfile(p, argv[i]);
						free(p);
					}
				} else {
					if (((strcmp(argv[i], "-nc")) == 0)
					    || ((strcmp(argv[i], "-C")) == 0))
						color = 0;

					if (((strcmp(argv[i], "-e")) == 0)
					    || ((strcmp(argv[i], "-exact")) == 0))
						exact = 1;
				}
			}
		}
		closedir(dir);
	}
	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}

int qlist_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	struct dirent *dentry, *de;
	char *cat, *p, *q;
	const char *path = "/var/db/pkg";
	struct stat st;
	size_t len = 0;
	char buf[_POSIX_PATH_MAX];
	char buf2[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return 1;

	p = q = cat = NULL;

	if (argc > 1) {
		cat = strchr(argv[1], '/');
		len = strlen(argv[1]);
	}
	while ((dentry = readdir(dir))) {
		if (*dentry->d_name == '.')
			continue;
		if ((strchr((char *) dentry->d_name, '-')) == 0)
			continue;
		stat(dentry->d_name, &st);
		if (!(S_ISDIR(st.st_mode)))
			continue;
		chdir(dentry->d_name);
		if ((dirp = opendir(".")) == NULL)
			continue;
		while ((de = readdir(dirp))) {
			if (*de->d_name == '.')
				continue;
			if (argc > 1) {
				if (cat != NULL) {
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name,
					         de->d_name);
					/*if ((rematch(argv[1], buf, REG_EXTENDED)) != 0)*/
					if ((strncmp(argv[1], buf, len)) != 0)
						continue;
				} else {
					/* if ((rematch(argv[1], de->d_name, REG_EXTENDED)) != 0)*/
					if ((strncmp(argv[1], de->d_name, len)) != 0)
						continue;
				}
			}
			if (argc < 2)
				printf("%s/%s\n", dentry->d_name, de->d_name);
			else {
				FILE *fp;
				snprintf(buf, sizeof(buf), "/var/db/pkg/%s/%s/CONTENTS",
				         dentry->d_name, de->d_name);
				if ((fp = fopen(buf, "r")) == NULL)
					continue;
				while ((fgets(buf, sizeof(buf), fp)) != NULL) {
					if ((p = strchr(buf, ' ')) == NULL)
						continue;
					++p;
					switch (*buf) {
					case '\n':   /* newline */
						break;
					case 'd':    /* dir */
						/*printf("%s", p);*/
						break;
					case 'o':    /* obj */
					case 's':    /* sym */
						strcpy(buf2, p);
						if ((p = strchr(buf2, ' ')) != NULL)
							*p = 0;
						printf("%s\n", buf2);
						break;
					default:
						break;
					}
				}
				fclose(fp);
			}
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	return 0;
}

void initialize_ebuild_flat(void)
{
	DIR *dir[3];
	struct dirent *dentry[3];
	FILE *fp;
	time_t start;

	if ((chdir(portdir)) != 0) {
		warn("Error: unable chdir to what I think is your PORTDIR '%s' : %s",
		     portdir, strerror(errno));
		return;
	}

	/* assuming --sync is used with --delete this will get recreated after every merged */
	if (access(".ebuild.x", W_OK) == 0)
		return;

	warn("Updating ebuild cache ... ");

	unlink(".ebuild.x");
	if (errno != ENOENT) {
		warnf("Error: unlinking %s/%s : %s", portdir,
		      ".ebuild.x", strerror(errno));
		return;
	}

	if ((fp = fopen(".ebuild.x", "w")) == NULL) {
		warnf("Error opening %s/.ebuild.x %s", portdir,
		      strerror(errno));
		return;
	}

	start = time(0);
	if ((dir[0] = opendir(".")) == NULL)
		return;

	while ((dentry[0] = readdir(dir[0]))) {
		struct stat st;
		stat(dentry[0]->d_name, &st);

		if (*dentry[0]->d_name == '.')
			continue;

		if (!(S_ISDIR(st.st_mode)))
			continue;

		if ((strchr(dentry[0]->d_name, '-')) == NULL)
			continue;

		if ((dir[1] = opendir(dentry[0]->d_name)) == NULL)
			continue;

		while ((dentry[1] = readdir(dir[1]))) {
			char de[_POSIX_PATH_MAX];
			if (*dentry[1]->d_name == '.')
				continue;

			snprintf(de, sizeof(de), "%s/%s", dentry[0]->d_name,
			         dentry[1]->d_name);

			stat(de, &st);
			if (!(S_ISDIR(st.st_mode)))
				continue;

			if ((dir[2] = opendir(de)) == NULL)
				continue;

			while ((dentry[2] = readdir(dir[2]))) {
				char *p;
				if (*dentry[2]->d_name == '.')
					continue;
				if ((p = rindex(dentry[2]->d_name, '.')) != NULL)
					if (strcmp(p, ".ebuild") == 0) {
						fprintf(fp, "%s/%s/%s\n", dentry[0]->d_name,
						        dentry[1]->d_name, dentry[2]->d_name);
					}
			}
			closedir(dir[2]);
		}
		closedir(dir[1]);
	}
	closedir(dir[0]);
	fclose(fp);
	warn("Finished in %lu seconds", time(0) - start);
}

void reinitialize_ebuild_flat(void)
{
	if ((chdir(portdir)) != 0) {
		warn("Error: unable chdir to what I think is your PORTDIR '%s' : %s",
		     portdir, strerror(errno));
		return;
	}
	unlink(".ebuild.x");
	initialize_ebuild_flat();
}

#define Q_FLAGS "i" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",   no_argument, NULL, 'i'},
	COMMON_LONG_OPTS
};
static const char *q_opts_help[] = {
	"Install symlinks for applets",
	COMMON_OPTS_HELP
};
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, Q_IDX)

int q_main(int argc, char **argv)
{
	int i;
	char *p;
	APPLET func;

	if (argc == 0)
		return 1;

	p = argv0 = basename(argv[0]);

	if ((func = lookup_applet(p)) == 0)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	while ((i=getopt_long(argc, argv, "+" Q_FLAGS, q_long_opts, NULL)) != -1) {
		switch (i) {

		case 'V': version_barf(); break;
		case 'h': q_usage(EXIT_SUCCESS); break;

		case 'i': {
			char buf[_POSIX_PATH_MAX];
			printf("Installing symlinks:\n");
			memset(buf, 0x00, sizeof(buf));
			if ((readlink("/proc/self/exe", buf, sizeof(buf))) == (-1)) {
				warnf("could not readlink '/proc/self/exe': %s", strerror(errno));
				return 1;
			}
			if (chdir(dirname(buf)) != 0) {
				warnf("could not chdir to '%s': %s", buf, strerror(errno));
				return 1;
			}
			for (i = 1; applets[i].name; ++i) {
				printf(" %s ...", applets[i].name);
				errno = 0;
				symlink("q", applets[i].name);
				printf("\t[%s]\n", strerror(errno));
			}
			return 0;
		}

		default: break;
		}
	}

	if ((func = lookup_applet(argv[1])) == 0)
		return 1;

	return (func)(argc - 1, ++argv);
}

void reinitialize_as_needed(void)
{
	if (reinitialize)
		reinitialize_ebuild_flat();
}

int main(int argc, char **argv)
{
	argv0 = argv[0];
	initialize_portdir();
	atexit(reinitialize_as_needed);
	return q_main(argc, argv);
}
