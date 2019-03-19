/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _MAIN_H
#define _MAIN_H 1

#ifdef HAVE_CONFIG_H
# include "config.h"  /* make sure we have EPREFIX, if set */
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "i18n.h"
#include "colors.h"

extern const char *argv0;

#ifndef DEFAULT_PORTAGE_BINHOST
# define DEFAULT_PORTAGE_BINHOST ""
#endif

#ifndef CONFIG_EPREFIX
# define CONFIG_EPREFIX "/"
#endif

/* make sure our buffers are as big as they can be */
#if PATH_MAX > _POSIX_PATH_MAX  /* _Q_PATH_MAX */
# define _Q_PATH_MAX PATH_MAX
#else
# define _Q_PATH_MAX _POSIX_PATH_MAX
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif

#ifndef MIN
# define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
# define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif

/* Easy enough to glue to older versions */
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif
#ifndef O_PATH
# define O_PATH 0
#endif

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define qfprintf(stream, fmt, args...) do { if (!quiet) fprintf(stream, _( fmt ), ## args); } while (0)
#define qprintf(fmt, args...) qfprintf(stdout, _( fmt ), ## args)

#define _q_unused_ __attribute__((__unused__))

#ifdef EBUG
# define DBG(fmt, args...) warnf(fmt , ## args)
# define IF_DEBUG(x) x
#else
# define DBG(fmt, args...)
# define IF_DEBUG(x)
#endif

#undef USE_CLEANUP
/* LSAN (Leak Sanitizer) will complain about things we leak. */
#ifdef __SANITIZE_ADDRESS__
# define USE_CLEANUP 1
#endif
/* Coverity catches some things we leak on purpose. */
#ifdef __COVERITY__
# define USE_CLEANUP 1
#endif
#ifndef USE_CLEANUP
# define USE_CLEANUP 0
#endif

#define GETOPT_LONG(A, a, ex) \
	getopt_long(argc, argv, ex A ## _FLAGS, a ## _long_opts, NULL)

#define a_argument required_argument
#define opt_argument optional_argument

/* we need the space before the last comma or we trigger a bug in gcc-2 :( */
FILE *warnout;
#if defined OPTIMIZE_FOR_SIZE && (OPTIMIZE_FOR_SIZE > 1)
#define warn(fmt, args...)
#else
#define warn(fmt, args...) \
	fprintf(warnout, _("%s%s%s: " fmt "\n"), RED, argv0, NORM , ## args)
#endif
#define warnf(fmt, args...) warn("%s%s()%s: " fmt, YELLOW, __func__, NORM , ## args)
#define warnl(fmt, args...) warn("%s%i()%s: " fmt, YELLOW, __LINE__, NORM , ## args)
#define warnp(fmt, args...) warn(fmt ": %s" , ## args , strerror(errno))
#define warnfp(fmt, args...) warnf(fmt ": %s" , ## args , strerror(errno))
#define _err(wfunc, fmt, args...) \
	do { \
	if (USE_CLEANUP) { \
		if (warnout != stderr) \
			fclose(warnout); \
	} \
	warnout = stderr; \
	wfunc(fmt , ## args); \
	exit(EXIT_FAILURE); \
	} while (0)
#define err(fmt, args...) _err(warn, fmt , ## args)
#define errf(fmt, args...) _err(warnf, fmt , ## args)
#define errp(fmt, args...) _err(warnp, fmt , ## args)
#define errfp(fmt, args...) _err(warnfp, fmt, ## args)

int rematch(const char *, const char *, int);

#endif
