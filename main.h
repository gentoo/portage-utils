/*
 * Copyright 2005-2018 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

/* make sure our buffers are as big as they can be */
#if PATH_MAX > _POSIX_PATH_MAX  /* _Q_PATH_MAX */
# define _Q_PATH_MAX PATH_MAX
#else
# define _Q_PATH_MAX _POSIX_PATH_MAX
#endif

/* http://tinderbox.dev.gentoo.org/default-linux/arm */
/* http://tinderbox.dev.gentoo.org/default-linux/hppa */

#ifdef __linux__
# undef URL_BASE
# define URL_BASE "http://tinderbox.dev.gentoo.org"

# undef URL_PROFILE
# ifdef __UCLIBC__
#  define URL_PROFILE "uclibc"
# else
#  ifdef __SSP__
#   define URL_PROFILE "hardened"
#  else
#   define URL_PROFILE "default/linux"
#  endif
# endif

# undef URL_ARCH
# if 0
# elif defined(__alpha__)
#  define URL_ARCH "alpha"
# elif defined(__x86_64__)
#  define URL_ARCH "amd64"
# elif defined(__arm__)
#  define URL_ARCH "arm"
# elif defined(__aarch64__)
#  define URL_ARCH "arm64"
# elif defined(__bfin__)
#  define URL_ARCH "bfin"
# elif defined(__cris__)
#  define URL_ARCH "cris"
# elif defined(__hppa__)
#  define URL_ARCH "hppa"
# elif defined(__ia64__)
#  define URL_ARCH "ia64"
# elif defined(__m68k__)
#  define URL_ARCH "m68k"
# elif defined(__mips__)
#  define URL_ARCH "mips"
# elif defined(__powerpc__)
#  if defined(__powerpc64__)
#   define URL_ARCH "ppc64"
#  else
#   define URL_ARCH "ppc"
#  endif
# elif defined(__s390__)
#  define URL_ARCH "s390"
# elif defined(__sh__)
#  define URL_ARCH "sh"
# elif defined(__sparc__)
#  define URL_ARCH "sparc"
# elif defined(__i386__)
#  define URL_ARCH "x86"
# endif

# if defined(URL_PROFILE) && defined(URL_ARCH)
#  define DEFAULT_PORTAGE_BINHOST URL_BASE "/" URL_PROFILE "/" URL_ARCH
# endif
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
