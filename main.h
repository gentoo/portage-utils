/*
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/main.h,v 1.15 2011/12/19 20:27:36 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2010 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _q_static
# define _q_static static
#endif

/* make sure our buffers are as big as they can be */
#if PATH_MAX > _POSIX_PATH_MAX  /* _Q_PATH_MAX */
# define _Q_PATH_MAX PATH_MAX
#else
# define _Q_PATH_MAX _POSIX_PATH_MAX
#endif

/* http://tinderbox.dev.gentoo.org/default-linux/arm */
/* http://tinderbox.dev.gentoo.org/default-linux/hppa */

#ifdef __linux__
#undef  URL
#define URL "http://tinderbox.dev.gentoo.org"
# ifdef __i386__
#  ifdef __UCLIBC__
#   define DEFAULT_PORTAGE_BINHOST URL "/uclibc/i386"
#  else
#   ifdef __SSP__
#    define DEFAULT_PORTAGE_BINHOST URL "/hardened/x86"
#   else
#    define DEFAULT_PORTAGE_BINHOST URL "/default-linux/x86/All"
#   endif
#  endif
#  if defined(__powerpc__) && defined(__SSP__)
#   if !defined(__UCLIBC__)
#    define DEFAULT_PORTAGE_BINHOST URL "/hardened/ppc"
#   else
#    define DEFAULT_PORTAGE_BINHOST URL "/uclibc/ppc"
#   endif
#  endif
# endif
#endif

#ifndef DEFAULT_PORTAGE_BINHOST
# define DEFAULT_PORTAGE_BINHOST ""
#endif

#define qfprintf(stream, fmt, args...) do { if (!quiet) fprintf(stream, _( fmt ), ## args); } while (0)
#define qprintf(fmt, args...) qfprintf(stdout, _( fmt ), ## args)

#define _q_unused_ __attribute__((__unused__))

#ifndef BUFSIZE
# define BUFSIZE 8192
#endif

#ifdef EBUG
# define DBG(fmt, args...) warnf(fmt , ## args)
# define IF_DEBUG(x) x
#else
# define DBG(fmt, args...)
# define IF_DEBUG(x)
#endif

#define GETOPT_LONG(A, a, ex) \
	getopt_long(argc, argv, ex A ## _FLAGS, a ## _long_opts, NULL)

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#endif

#define a_argument required_argument

/* Easy enough to glue to older versions */
#ifndef O_CLOEXEC
# define O_CLOEXEC 0
#endif

#ifndef CONFIG_EPREFIX
#define CONFIG_EPREFIX "/"
#endif
