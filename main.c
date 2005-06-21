/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/main.c,v 1.45 2005/06/21 22:28:07 vapier Exp $
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
#include <sys/time.h>
#include <ctype.h>
#include <dirent.h>
#include <getopt.h>
#include <regex.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <assert.h>



/* prototypes and such */
static char eat_file(const char *file, char *buf, const size_t bufsize);
int rematch(const char *, const char *, int);
static char *rmspace(char *);

char *initialize_portdir(void);
void initialize_ebuild_flat(void);
void reinitialize_ebuild_flat(void);
void reinitialize_as_needed(void);
#define _q_unused_ __attribute__((__unused__))



/* color constants */
#define COLOR(c,b) (color ? "\e[" c ";" b "m" : "")
#define BOLD      COLOR("00", "01")
#define NORM      COLOR("00", "00")
#define BLUE      COLOR("36", "01")
#define DKBLUE    COLOR("34", "01")
#define CYAN      COLOR("36", "02")
#define GREEN     COLOR("32", "01")
#define MAGENTA   COLOR("35", "02")
#define RED       COLOR("31", "01")
#define YELLOW    COLOR("33", "01")



/* helper functions for showing errors */
static const char *argv0;
#define warn(fmt, args...) \
	fprintf(stderr, "%s%s%s: " fmt "\n", RED, argv0, NORM, ## args)
#define warnf(fmt, args...) warn("%s%s()%s: " fmt, YELLOW, __FUNCTION__, NORM, ## args)
#define warnp(fmt, args...) warn(fmt ": %s", ## args, strerror(errno))
#define warnfp(fmt, args...) warnf(fmt ": %s", ## args, strerror(errno))
#define _err(wfunc, fmt, args...) \
	do { \
	wfunc(fmt, ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#define err(fmt, args...) _err(warn, fmt, ## args)
#define errf(fmt, args...) _err(warnf, fmt, ## args)
#define errp(fmt, args...) _err(warnp, fmt, ## args)
#ifdef EBUG
#include <sys/resource.h>

# define DBG(fmt, args...) warnf(fmt, ## args)
# define IF_DEBUG(x) x
void init_coredumps(void);
void init_coredumps(void)
{
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

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif



/* variables to control runtime behavior */
static const char *rcsid = "$Id: main.c,v 1.45 2005/06/21 22:28:07 vapier Exp $";

static char color = 1;
static char exact = 0;
static int found = 0;
static int verbose = 0;
static char reinitialize = 0;

static char portdir[_POSIX_PATH_MAX] = "/usr/portage";
static char portvdb[] = "/var/db/pkg";
static char portcachedir[] = "metadata/cache";


/* include common library code */
#include "libq/libq.c"

/* include common applet defs */
#include "applets.h"

/* Common usage for all applets */
#define COMMON_FLAGS "vChV"
#define a_argument required_argument
#define COMMON_LONG_OPTS \
	{"verbose",   no_argument, NULL, 'v'}, \
	{"nocolor",   no_argument, NULL, 'C'}, \
	{"help",      no_argument, NULL, 'h'}, \
	{"version",   no_argument, NULL, 'V'}, \
	{NULL,        no_argument, NULL, 0x0}
#define COMMON_OPTS_HELP \
	"Make a lot of noise", \
	"Don't output color", \
	"Print this help and exit", \
	"Print version and exit", \
	NULL
#define COMMON_GETOPTS_CASES(applet) \
	case 'v': ++verbose; break; \
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
		printf("%sUsage:%s %sq%s %s<applet> [arguments]...%s\n\n", GREEN, 
			NORM, YELLOW, NORM, DKBLUE, NORM);
		printf("%sCurrently defined applets:%s\n", GREEN, NORM);
		for (i = FIRST_APPLET; i <= LAST_APPLET; ++i)
			printf(" %s*%s %s%s%s %s%s%s\t%s:%s %s\n", GREEN, NORM,
				YELLOW, applets[i].name, NORM, 
				DKBLUE, applets[i].opts, NORM,
				RED, NORM, applets[i].desc);
	} else {
		printf("%sUsage:%s %s%s%s %s%s%s %s:%s %s\n", GREEN, NORM,
			YELLOW, applets[blabber].name, NORM,
			DKBLUE, applets[blabber].opts, NORM,
			RED, NORM, applets[blabber].desc);
	}

	printf("\n%sOptions:%s -[%s]\n", GREEN, NORM, flags);
	for (i = 0; opts[i].name; ++i) {
		assert(help[i] != NULL); /* this assert is a life saver when adding new applets. */
		if (opts[i].has_arg == no_argument)
			printf("  -%c, --%-13s%s*%s %s\n", opts[i].val,
			       opts[i].name, RED, NORM, help[i]);
		else
			printf("  -%c, --%-6s %s<arg>%s %s*%s %s\n", opts[i].val,
			       opts[i].name, DKBLUE, NORM, RED, NORM, help[i]);
	}
	exit(status);
}
static void version_barf(void)
{
#ifndef VERSION
# define VERSION "cvs"
#endif
	printf("portage-utils-%s: compiled %s\n%s\n"
	       "%s written for Gentoo by <solar and vapier @ gentoo.org>\n",
	       VERSION, __DATE__, rcsid, argv0);
	exit(EXIT_SUCCESS);
}

static char eat_file(const char *file, char *buf, const size_t bufsize)
{
	FILE *f;
	struct stat s;
	char ret = 0;

	if ((f = fopen(file, "r")) == NULL)
		return ret;

	memset(buf, 0x00, bufsize);
	if (fstat(fileno(f), &s) != 0)
		goto close_and_ret;
	if (s.st_size) {
		if (bufsize < (size_t)s.st_size)
			goto close_and_ret;
		if (fread(buf, 1, s.st_size, f) != (size_t)s.st_size)
			goto close_and_ret;
	} else {
		if (fread(buf, 1, bufsize, f) == 0)
			goto close_and_ret;
	}

	ret = 1;
close_and_ret:
	fclose(f);
	return ret;
}

int rematch(const char *regex, const char *match, int cflags)
{
	regex_t preg;
	int ret;

	if ((match == NULL) || (regex == NULL))
		return EXIT_FAILURE;

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
static char *rmspace(char *s)
{
	register char *p;
	/* find the start of trailing space and set it to \0 */
	for (p = s + strlen(s) - 1; (isspace(*p) && p >= s); --p);
	if (p != s + strlen(s) - 1)
		*(p + 1) = 0;
	/* find the end of leading space and set p to it */
	for (p = s; (isspace(*p) && *p); ++p);
	/* move the memory backward to overwrite leading space */
	if (p != s)
		memmove(s, p, strlen(p)+1);
	return s;
}

/* removes adjacent extraneous white space */
static char *remove_extra_space(char *str);
static char *remove_extra_space(char *str)
{
	char *p, c = ' ';
	size_t len, pos = 0;
	char *buf;

	if (str == NULL) return NULL;
	len = strlen(str);
	buf = (char*)xmalloc(len+1);
	memset(buf, 0, len+1);
	for (p = str; *p != 0; ++p) {
		if (!isspace(*p)) c = *p; else {
			if (c == ' ') continue;
			c = ' ';
		}
		buf[pos] = c;
		pos++;
	}
	if (pos > 0 && buf[pos-1] == ' ') buf[pos-1] = '\0';
	strcpy(str, buf);
	free(buf);
	return str;
}

char *initialize_portdir(void)
{
	FILE *fp;
	char buf[_POSIX_PATH_MAX + 8];
	char *p = getenv("PORTDIR");
	size_t i;

	if (p) {
		if ((size_t)strlen(p) <= sizeof(portdir)) {
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
			if (strchr(buf, '$') != NULL) {
				warn("Sorry bash variables for your PORTDIR make us cry");
				continue;
			}

			for (i = 8; i < (size_t)strlen(buf); i++)
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
enum {
	CACHE_EBUILD = 1,
	CACHE_METADATA = 2
};

int filter_hidden(const struct dirent *dentry);
int filter_hidden(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	return 1;
}

#define CACHE_EBUILD_FILE ".ebuild.x"
#define CACHE_METADATA_FILE ".metadata.x"
const char *initialize_flat(int cache_type);
const char *initialize_flat(int cache_type)
{
	struct dirent **category, **pn, **eb;
	struct stat st;
	struct timeval start, finish;
	static const char *cache_file;
	char *p;
	int a, b, c, d, e, i;
	int frac, secs, count;
	FILE *fp;

	a = b = c = d = e = i = 0;
	count = frac = secs = 0;

	cache_file = (cache_type == CACHE_EBUILD ? CACHE_EBUILD_FILE : CACHE_METADATA_FILE);

	if (chdir(portdir) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		goto ret;
	}

	if (cache_type == CACHE_METADATA && chdir(portcachedir) != 0) {
		warnp("chdir to portage cache '%s/%s' failed", portdir, portcachedir);
		goto ret;
	}

	if ((stat(cache_file, &st)) != (-1))
		if (st.st_size == 0)
			unlink(cache_file);

	/* assuming --sync is used with --delete this will get recreated after every merged */
	if (access(cache_file, R_OK) == 0)
		goto ret;

	warn("Updating ebuild cache ... ");

	unlink(cache_file);
	if (errno != ENOENT) {
		warnfp("unlinking '%s/%s' failed", portdir, cache_file);
		goto ret;
	}

	if ((fp = fopen(cache_file, "w")) == NULL) {
		warnfp("opening '%s/%s' failed", portdir, cache_file);
		goto ret;
	}

	gettimeofday(&start, NULL);

	if ((a = scandir(".", &category, filter_hidden, alphasort)) < 0)
		goto ret;

	for (i = 0 ; i < a; i++) {
		stat(category[i]->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (strchr(category[i]->d_name, '-') == NULL)
			continue;

		if ((b = scandir(category[i]->d_name, &pn, filter_hidden, alphasort)) < 0)
			continue;
		for (c = 0; c < b; c++) {
			char de[_POSIX_PATH_MAX];

			snprintf(de, sizeof(de), "%s/%s", category[i]->d_name, pn[c]->d_name);

			if (stat(de, &st) < 0)
				continue;

			switch (cache_type) {
			case CACHE_EBUILD:
				if (!S_ISDIR(st.st_mode))
					continue;
				break;
			case CACHE_METADATA:
				if (S_ISREG(st.st_mode))
					fprintf(fp, "%s\n", de);
				continue;
				break;
			}
			if ((e = scandir(de, &eb, filter_hidden, alphasort)) < 0)
				continue;
			for (d = 0 ; d < e; d++) {
				if ((p = rindex(eb[d]->d_name, '.')) != NULL)
					if (strcmp(p, ".ebuild") == 0) {
						count++;
						fprintf(fp, "%s/%s/%s\n", category[i]->d_name, pn[c]->d_name, eb[d]->d_name);
					}
			}
			while(d--) free(eb[d]);
			free(eb);
		}
		while(b--) free(pn[b]);
		free(pn);
	}
	fclose(fp);
	while(a--) free(category[a]);
	free(category);
        
	gettimeofday(&finish, NULL);
	if (start.tv_usec > finish.tv_usec) {
		finish.tv_usec += 1000000;
		finish.tv_sec--;
        
	}
	frac = (finish.tv_usec - start.tv_usec);
	secs = (finish.tv_sec - start.tv_sec);
	if (secs < 0) secs = 0;
	if (frac < 0) frac = 0;
	warn("Finished %u entries in %d.%06d seconds", count, secs, frac);
ret:
	return cache_file;
}
#define initialize_ebuild_flat() initialize_flat(CACHE_EBUILD)
#define initialize_metadata_flat() initialize_flat(CACHE_METADATA)

void reinitialize_ebuild_flat(void)
{
	if ((chdir(portdir)) != 0) {
		warnp("chdir to PORTDIR '%s' failed", portdir);
		return;
	}
	unlink(CACHE_EBUILD_FILE);
	initialize_ebuild_flat();
}

void reinitialize_as_needed(void)
{
	if (reinitialize)
		reinitialize_ebuild_flat();
}

typedef struct {
	char *_data;
	char *DEPEND;        /* line 1 */
	char *RDEPEND;
	char *SLOT;
	char *SRC_URI;
	char *RESTRICT;      /* line 5 */
	char *HOMEPAGE;
	char *LICENSE;
	char *DESCRIPTION;
	char *KEYWORDS;
	char *INHERITED;     /* line 10 */
	char *IUSE;
	char *CDEPEND;
	char *PDEPEND;
	char *PROVIDE;       /* line 14 */
	depend_atom *atom;
} portage_cache;

void cache_free(portage_cache *cache);
portage_cache *cache_read_file(const char *file);
portage_cache *cache_read_file(const char *file)
{
	struct stat s;
	char *ptr;
	FILE *f;
	portage_cache *ret = NULL;
	size_t len;

	if ((f = fopen(file, "r")) == NULL)
		goto err;

	if (fstat(fileno(f), &s) != 0)
		goto err;
	len = sizeof(*ret) + s.st_size + 1;
	ret = xmalloc(len);
	memset(ret, 0x00, len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->atom = atom_explode(file);

	ret->DEPEND = ret->_data;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->curr, '\n')) == NULL) { \
		warn("Invalid cache file '%s'", file); \
		goto err; \
	} \
	ret->next = ptr+1; \
	*ptr = '\0';
	next_line(DEPEND, RDEPEND)
	next_line(RDEPEND, SLOT)
	next_line(SLOT, SRC_URI)
	next_line(SRC_URI, RESTRICT)
	next_line(RESTRICT, HOMEPAGE)
	next_line(HOMEPAGE, LICENSE)
	next_line(LICENSE, DESCRIPTION)
	next_line(DESCRIPTION, KEYWORDS)
	next_line(KEYWORDS, INHERITED)
	next_line(INHERITED, IUSE)
	next_line(IUSE, CDEPEND)
	next_line(CDEPEND, PDEPEND)
	next_line(PDEPEND, PROVIDE)
#undef next_line
	ptr = strchr(ptr+1, '\n');
	*ptr = '\0';

	fclose(f);

	return ret;

err:
	if (ret) cache_free(ret);
	return NULL;
}

void cache_dump(portage_cache *cache);
void cache_dump(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");

	printf("DEPEND     : %s\n", cache->DEPEND);
	printf("RDEPEND    : %s\n", cache->RDEPEND);
	printf("SLOT       : %s\n", cache->SLOT);
	printf("SRC_URI    : %s\n", cache->SRC_URI);
	printf("RESTRICT   : %s\n", cache->RESTRICT);
	printf("HOMEPAGE   : %s\n", cache->HOMEPAGE);
	printf("LICENSE    : %s\n", cache->LICENSE);
	printf("DESCRIPTION: %s\n", cache->DESCRIPTION);
	printf("KEYWORDS   : %s\n", cache->KEYWORDS);
	printf("INHERITED  : %s\n", cache->INHERITED);
	printf("IUSE       : %s\n", cache->IUSE);
	printf("CDEPEND    : %s\n", cache->CDEPEND);
	printf("PDEPEND    : %s\n", cache->PDEPEND);
	printf("PROVIDE    : %s\n", cache->PROVIDE);
	if (!cache->atom) return;
	printf("CATEGORY   : %s\n", cache->atom->CATEGORY);
	printf("PN         : %s\n", cache->atom->PN);
	printf("PV         : %s\n", cache->atom->PV);
	printf("PVR        : %s\n", cache->atom->PVR);
}

void cache_free(portage_cache *cache)
{
	if (!cache)
		errf("Cache is empty !");
	atom_implode(cache->atom);
	free(cache);
}

#include "q.c"
#include "qcheck.c"
#include "qdepends.c"
#include "qfile.c"
#include "qlist.c"
#include "qlop.c"
#include "qsearch.c"
#include "qsize.c"
#include "qtbz2.c"
#include "quse.c"

int main(int argc, char **argv)
{
	IF_DEBUG(init_coredumps());
	argv0 = argv[0];
	if (getenv("NOCOLOR"))
		color = 0;
	initialize_portdir();
	atexit(reinitialize_as_needed);
	return q_main(argc, argv);
}
