/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 *
 * All the junk in one trunk!
 */

#ifndef _PORTING_H
#define _PORTING_H

#ifdef HAVE_CONFIG_H
# include "config.h"  /* make sure we have EPREFIX, if set */
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64 /* #471024 */
#endif
#ifdef _AIX
#define _LINUX_SOURCE_COMPAT
#endif

#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#if defined(__MACH__)
#include <libproc.h>
#endif

#if defined(__linux__)
# include <endian.h>
# include <byteswap.h>
#elif defined(__FreeBSD__)
# include <sys/endian.h>
#endif

/* Solaris */
#if defined(__sun) && defined(__SVR4)
# include <sys/dklabel.h>
# define S_BLKSIZE DK_DEVID_BLKSIZE
#elif defined(__hpux__) || defined(__MINT__)
/* must not include both dir.h and dirent.h on hpux11..11 & FreeMiNT */
#elif defined(__linux__)
/* Linux systems do not need sys/dir.h as they are generally POSIX sane */
#else
# include <sys/dir.h>
#endif

/* AIX */
#ifdef _AIX
# include <sys/stat.h>
# define S_BLKSIZE DEV_BSIZE
#endif

/* Windows Interix */
#ifdef __INTERIX
# define S_BLKSIZE S_BLOCK_SIZE
#endif

/* HP-UX */
#ifdef __hpux
# define S_BLKSIZE st.st_blksize
#endif

/* Everyone else */
#ifndef S_BLKSIZE
# define S_BLKSIZE 512
#endif

#if defined(__sun) && defined(__SVR4)
/* workaround non-const defined name in option struct, such that we
 * don't get a zillion of warnings */
#define	no_argument		0
#define	required_argument	1
#define	optional_argument	2
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};
extern int	getopt_long(int, char * const *, const char *,
		    const struct option *, int *);
#else
# include <getopt.h>
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

#ifndef CONFIG_EPREFIX
# define CONFIG_EPREFIX "/"
#endif

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#endif
