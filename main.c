/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/main.c,v 1.13 2005/06/13 03:12:42 vapier Exp $
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
#include <assert.h>



/* prototypes and such */
typedef int (*APPLET)(int, char **);

APPLET lookup_applet(char *);

int rematch(char *, const char *, int);
char *rmspace(char *);
void qfile(char *, char *);

char *initialize_portdir(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);



/* helper functions for showing errors */
static char *argv0;
#define warn(fmt, args...) \
	fprintf(stderr, "%s: " fmt "\n", argv0, ## args)
#define warnf(fmt, args...) warn("%s(): " fmt, __FUNCTION__, ## args)
#define warnp(fmt, args...) warn(fmt ": %s", ## args, strerror(errno))
#define warnfp(fmt, args...) warnf(fmt ": %s", ## args, strerror(errno))
#define err(fmt, args...) \
	do { \
	warn(fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#ifdef EBUG
#include <sys/resource.h>

# define DBG(fmt, args...) warnf(fmt, ## args)
# define IF_DEBUG(x) x
void init_coredumps(void);
void init_coredumps(void) {
	struct rlimit rl;
	int val = 0;
	val = RLIM_INFINITY;
	rl.rlim_cur = val;
	rl.rlim_max = val;
	setrlimit(RLIMIT_CORE, &rl);
}
#else
# define DBG(fmt, args...)
# define IF_DEBUG(x)
#endif


/* variables to control runtime behavior */
static const char *rcsid = "$Id: main.c,v 1.13 2005/06/13 03:12:42 vapier Exp $";

static char color = 1;
static char exact = 0;
static int found = 0;
static char reinitialize = 0;

static char portdir[_POSIX_PATH_MAX] = "/usr/portage";
static char portvdb[] = "/var/db/pkg";



/* color constants */
#define COLOR(c,b) (color ? "\e[" c ";" b "m" : "")
#define BOLD      COLOR("00", "01")
#define BLUE      COLOR("36", "01")
#define CYAN      COLOR("36", "02")
#define MAGENTA   COLOR("35", "02")
#define GREE      COLOR("32", "01")
#define RED       COLOR("31", "01")
#define NORM      COLOR("00", "00")


/* applet prototypes */
int q_main(int, char **);
int qsearch_main(int, char **);
int quse_main(int, char **);
int qlist_main(int, char **);
int qfile_main(int, char **);
int qsize_main(int, char **);
int qcheck_main(int, char **);

/* applets we support */
typedef enum {
	FIRST_APPLET = 0,
	APPLET_Q = 0,
	APPLET_QFILE = 1,
	APPLET_QLIST = 2,
	APPLET_QSEARCH = 3,
	APPLET_QUSE = 4,
	APPLET_QSIZE = 5,
	APPLET_QCHECK = 6,
	LAST_APPLET = 6
} applets_enum;
struct applet_t {
	const char *name;
	/* int *func; */
	APPLET func;
	const char *opts;
} applets[] = {
	/* q must always be the first applet */
	{"q",         q_main,         "<applet> <args>",},
	{"qfile",     qfile_main,     "<filename>"},
	{"qlist",     qlist_main,     "<pkgname>"},
	{"qsearch",   qsearch_main,   "<regex>"},
	{"quse",      quse_main,      "<useflag>"},
	{"qsize",     qsize_main,     "<pkgname>"},
	{"qcheck",    qcheck_main,    "<pkgname>"},

#ifdef EQUERY_COMPAT
	/* aliases for equery capatability */
	{"belongs",   qfile_main,     "<filename>"},
	/*"changes"*/
	{"check",     qcheck_main,    "<pkgname>"},
	/*"depends"*/
	/*"depgraph"*/
	{"files",     qlist_main,     "<pkgname>"},
	/*"glsa"*/
	{"hasuse",    quse_main,      "<useflag>"},
	/*"list"*/
	{"size",      qsize_main,     "<pkgname>"},
	/*"stats"*/
	/*"uses"*/
	/*"which"*/
#endif
	{NULL,      NULL,         NULL}
};



/* Common usage for all applets */
#define COMMON_FLAGS "ChV"
#define a_argument required_argument
#define COMMON_LONG_OPTS \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 'V': version_barf(); break; \
	case 'h': applet ## _usage(EXIT_SUCCESS); break; \
	case 'C': color = 0; break; \
	default: applet ## _usage(EXIT_FAILURE); break;
#define GETOPT_LONG(A, a, ex) \
	getopt_long(argc, argv, ex A ## _FLAGS, a ## _long_opts, NULL)
/* display usage and exit */
static void usage(int status, const char *flags, struct option const opts[], 
                  const char *help[], applets_enum blabber)
{
	unsigned long i;
	if (blabber == APPLET_Q) {
		printf("Usage: q <applet> [arguments]...\n\n");
		printf("Currently defined applets:\n");
		for (i = FIRST_APPLET; i <= LAST_APPLET; ++i)
			printf(" - %s %s\n", applets[i].name, applets[i].opts);
	} else {
		printf("Usage: %s %s\n", applets[blabber].name, applets[blabber].opts);
	}

	printf("\nOptions: -[%s]\n", flags);
	for (i = 0; opts[i].name; ++i) {
		assert(help[i] != NULL);
		if (opts[i].has_arg == no_argument)
			printf("  -%c, --%-13s* %s\n", opts[i].val, 
			       opts[i].name, help[i]);
		else
			printf("  -%c, --%-6s <arg> * %s\n", opts[i].val,
			       opts[i].name, help[i]);
	}
	exit(status);
}
static void version_barf(void)
{
#ifndef VERSION
# define VERSION "cvs"
#endif
	printf("portage-utils-%s: %s compiled %s\n%s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       VERSION, __FILE__, __DATE__, rcsid, argv0);
	exit(EXIT_SUCCESS);
}

APPLET lookup_applet(char *applet)
{
	unsigned int i;
	for (i = 0; applets[i].name; ++i) {
		if (strcmp(applets[i].name, applet) == 0) {
			DBG("found applet %s at %p", applets[i].name, applets[i].func);
			return applets[i].func;
		}
	}
	/* No applet found? Search by shortname then... */
	if (strlen(applet) > 1) {
		DBG("Looking up applet (%s) by short name", applet);
		for (i = 1; applets[i].name; ++i) {
			if (strcmp(applets[i].name + 1, applet) == 0) {
				DBG("found applet by short name %s", applets[i].name);
				return applets[i].func;
			}
		}
	}
	/* still nothing ?  those bastards ... */
	warn("Unknown applet '%s'", applet);
	return 0;
}

int rematch(char *regex, const char *match, int cflags)
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
/* removed leading/trailing extraneous white space */
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

/* removes adjacent extraneous white space */
static char *rmextraneousspace(char *str);
static char *rmextraneousspace(char *str) {
	char *p, c = ' ';
	size_t len, pos = 0;
	char *buf;

	if (str == NULL) return NULL;
	len = strlen(str);
	buf = (char *) malloc(len+1);
	memset(buf, 0, len+1);
	for (p = str; *p != 0; ++p) {
		if (!isspace(*p)) c = *p; else {
			if (c == ' ') continue;
			c = ' ';
		}
		buf[pos] = c;
		pos++;
	}
	strcpy(str, buf);
	free(buf);
	return (char *) str;
}

char *initialize_portdir(void)
{
	FILE *fp;
	char buf[_POSIX_PATH_MAX + 8];
	char *p = getenv("PORTDIR");
	size_t i;

	if (getenv("NOCOLOR"))
		color = 0;

	if (p) {
		if (strlen(p) + 1 < sizeof(portdir)) {
			strcpy(portdir, p);
			return portdir;
		}
	}
	if ((fp = fopen("/etc/make.conf", "r")) != NULL) {
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			if (strncmp(buf, "NOCOLOR=", 8) == 0)
				color = 0;
			if (*buf != 'P')
				continue;
			if (strncmp(buf, "PORTDIR=", 8) != 0)
				continue;
			/* Sorry don't understand bash variables. */
			if (strchr(buf, '$') != NULL)
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
/* The logic for ebuild.x should be moved into /var/cache */
/* and allow for user defined --cache files */
#define EBUILD_CACHE ".ebuild.x"
void initialize_ebuild_flat(void)
{
	DIR *dir[3];
	struct dirent *dentry[3];
	FILE *fp;
	time_t start;

	if (chdir(portdir) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		return;
	}

	/* assuming --sync is used with --delete this will get recreated after every merged */
	if (access(EBUILD_CACHE, R_OK) == 0)
		return;

	warn("Updating ebuild cache ... ");

	unlink(EBUILD_CACHE);
	if (errno != ENOENT) {
		warnfp("unlinking '%s/%s' failed", portdir, EBUILD_CACHE);
		return;
	}

	if ((fp = fopen(EBUILD_CACHE, "w")) == NULL) {
		warnfp("opening '%s/%s' failed", portdir, EBUILD_CACHE);
		return;
	}

	start = time(NULL);
	if ((dir[0] = opendir(".")) == NULL)
		return;

	while ((dentry[0] = readdir(dir[0])) != NULL) {
		struct stat st;
		if (*dentry[0]->d_name == '.')
			continue;
		stat(dentry[0]->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (strchr(dentry[0]->d_name, '-') == NULL)
			continue;
		if ((dir[1] = opendir(dentry[0]->d_name)) == NULL)
			continue;

		while ((dentry[1] = readdir(dir[1])) != NULL) {
			char de[_POSIX_PATH_MAX];
			if (*dentry[1]->d_name == '.')
				continue;

			snprintf(de, sizeof(de), "%s/%s", dentry[0]->d_name,
			         dentry[1]->d_name);

			stat(de, &st);
			if (!S_ISDIR(st.st_mode))
				continue;

			if ((dir[2] = opendir(de)) == NULL)
				continue;

			while ((dentry[2] = readdir(dir[2])) != NULL) {
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
	warn("Finished in %lu seconds", (time_t)time(NULL) - start);
}

void reinitialize_ebuild_flat(void)
{
	if ((chdir(portdir)) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		return;
	}
	unlink(EBUILD_CACHE);
	initialize_ebuild_flat();
}

void reinitialize_as_needed(void)
{
	if (reinitialize)
		reinitialize_ebuild_flat();
}

#include "libq/libq.c"
#include "qfile.c"
#include "qlist.c"
#include "qsearch.c"
#include "quse.c"
#include "qsize.c"
#include "qcheck.c"
#include "q.c"

int main(int argc, char **argv)
{
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];
	initialize_portdir();
	atexit(reinitialize_as_needed);
	return q_main(argc, argv);
}
