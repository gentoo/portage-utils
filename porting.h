/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
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
#include <getopt.h>
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

#include <iniparser.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif

/* Easy enough to glue to older versions */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_PATH
#define O_PATH 0
#endif

#ifndef CONFIG_EPREFIX
#define CONFIG_EPREFIX "/"
#endif

#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#endif
