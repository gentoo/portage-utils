/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.10 2005/06/07 02:17:24 solar Exp $
 *
 * 2005 Ned Ludd	- <solar@gentoo.org>
 * 2005 Mike Frysinger	- <vapier@gentoo.org>
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

static const char *rcsid = "$Id: q.c,v 1.10 2005/06/07 02:17:24 solar Exp $";

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

#include "qfile.c"
#include "qlist.c"
#include "qsearch.c"
#include "quse.c"

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
