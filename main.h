/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

/* make sure our buffers are as big as they can be */
#if PATH_MAX > _POSIX_PATH_MAX  /* _Q_PATH_MAX */
# define _Q_PATH_MAX PATH_MAX
#else
# define _Q_PATH_MAX _POSIX_PATH_MAX
#endif

#ifndef DEFAULT_PORTAGE_BINHOST
# define DEFAULT_PORTAGE_BINHOST ""
#endif

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
