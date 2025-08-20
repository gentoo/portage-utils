# Copyright 2011-2025 Gentoo Authors
# Distributed under the terms of the GNU General Public License v2
#
# Copyright 2011-2014 Mike Frysinger  - <vapier@gentoo.org>
# Copyright 2011-     Fabian Groffen  - <grobian@gentoo.org>

AC_PREREQ([2.71])
AC_INIT([portage-utils],[git])
AM_INIT_AUTOMAKE([1.11 dist-xz foreign no-dist-gzip silent-rules -Wall])
AM_SILENT_RULES([yes]) # AM_INIT_AUTOMAKE([silent-rules]) is broken atm
AM_MAINTAINER_MODE([enable])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([autotools/m4])

AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_OPENMP
AM_PROG_CC_C_O

gl_EARLY
gl_INIT

AM_PROG_AR
AC_PROG_LN_S

headers='#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
'
AC_CHECK_HEADERS([stddef.h unistd.h])
AX_COMPILE_CHECK_SIZEOF([size_t],[${headers}])

AC_CHECK_FUNCS_ONCE(m4_flatten([
	   fmemopen
	   scandirat
]))

AC_CHECK_SENDFILE

AC_ARG_WITH([eprefix], [AS_HELP_STRING([--with-eprefix], [path for Gentoo/Prefix project])])
# ensure eprefix ends with a slash, since the code base expects that
case "$with_eprefix" in
	*/) with_eprefix="$with_eprefix" ;;
	*)  with_eprefix="${with_eprefix}/" ;;
esac
AC_DEFINE_UNQUOTED([CONFIG_EPREFIX], ["$with_eprefix"],
				   [Gentoo Prefix offset path])
AC_SUBST([CONFIG_EPREFIX], ["$with_eprefix"])

AC_ARG_ENABLE([qmanifest], [AS_HELP_STRING([--enable-qmanifest],
			  [support qmanifest applet])])

# always check libb2, gpgme and libarchive, necessary for gpkg support
PKG_CHECK_MODULES([LIBBL2], [libb2],
                  [
                    AC_DEFINE([HAVE_BLAKE2B], [1], [Define if you have blake2b])
                    LIBBL2="yes"
                  ],
                  [
                    AS_IF([test "x${enable_qmanifest}" = "xyes"],
                          [
                            AC_MSG_FAILURE([--enable-qmanifest was given, but libb2.pc could not be found])
                          ])
                    LIBBL2="no: missing dependencies"
                  ])

PKG_CHECK_MODULES([GPGME], [gpgme],
                  [
                    AC_DEFINE([HAVE_GPGME], [1], [Define if you have gpgme])
                    GPGME="yes"
                  ],
                  [
                    AS_IF([test "x${enable_qmanifest}" = "xyes"],
                          [
                            AC_MSG_FAILURE([--enable-qmanifest was given, but gpgme.pc could not be found])
                          ])
                    GPGME="no: missing dependencies"
                  ])

PKG_CHECK_MODULES([LIBARCHIVE], [libarchive],
                  [
                    AC_DEFINE([HAVE_LIBARCHIVE], [1], [Define if you have libarchive])
                    LIBARCHIVE="yes"
                  ],
                  [
                    LIBARCHIVE="no: missing dependencies"
                  ])

AS_IF([test "x${enable_qmanifest}" != "xno"], [
  PKG_CHECK_MODULES([LIBZ], [zlib], [
    AC_DEFINE([HAVE_LIBZ], [1], [Define if you have zlib])
    LIBZ="yes"
  ], [
    AS_IF([test "x${enable_qmanifest}" = "xyes"], [
      AC_MSG_FAILURE([--enable-qmanifest was given, but zlib.pc could not be found])
    ])
    LIBZ="no: missing dependencies"
  ])

  AC_MSG_CHECKING([whether to enable qmanifest])
  AS_IF([test "x${LIBBL2}${LIBZ}${GPGME}" = "xyesyesyes"], [
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no: missing dependencies])
  ])
], [
  AC_MSG_CHECKING([whether to enable qmanifest])
  AC_MSG_RESULT([no: disabled by configure argument])
])

AM_CONDITIONAL([QMANIFEST_ENABLED], [test "x$enable_qmanifest" != xno])
if test "x$enable_qmanifest" != xno ; then
	AC_DEFINE([ENABLE_QMANIFEST], [1],
			  [Define if qmanifest should be compiled])
fi

AX_CFLAGS_WARN_ALL
AC_DEFUN([PT_CHECK_CFLAG],[AX_CHECK_COMPILER_FLAGS([$1],[CFLAGS="$CFLAGS $1"])])
m4_foreach_w([flag], [
	-Wunused
	-Wimplicit
	-Wshadow
	-Wformat=2
	-Wmissing-declarations
	-Wwrite-strings
	-Wbad-function-cast
	-Wnested-externs
	-Wcomment
	-Winline
	-Wchar-subscripts
	-Wcast-align
	-Wsequence-point
	-Wold-style-definition
	-Wextra
	-Wno-format-nonliteral
	-Wno-expansion-to-defined
], [
	AX_CHECK_COMPILE_FLAG(flag, AS_VAR_APPEND([CFLAGS], " flag"))
])
# gnulib triggers this a lot, just to silence:
# -Wno-format-nonliteral
# -Wno-expansion-to-defined

AC_CONFIG_FILES([
	Makefile
	libq/Makefile
	autotools/gnulib/Makefile
	tests/init.sh
	tests/Makefile
	tests/atom_compare/Makefile
	tests/atom_explode/Makefile
	tests/copy_file/Makefile
	tests/install/Makefile
	tests/mkdir/Makefile
	tests/profile/Makefile
	tests/qatom/Makefile
	tests/qcheck/Makefile
	tests/qdepends/Makefile
	tests/qfile/Makefile
	tests/qlist/Makefile
	tests/qlop/Makefile
	tests/qmanifest/Makefile
	tests/qmerge/Makefile
	tests/qtbz2/Makefile
	tests/quse/Makefile
	tests/qxpak/Makefile
	tests/rmspace/Makefile
	tests/source/Makefile
])
AC_OUTPUT

# vim: set ts=2 sw=2 softtabstop=2 expandtab:
