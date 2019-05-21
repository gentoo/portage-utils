AC_PREREQ([2.65])
AC_INIT([portage-utils], [git])
AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip silent-rules -Wall])
AM_SILENT_RULES([yes]) # AM_INIT_AUTOMAKE([silent-rules]) is broken atm
AM_MAINTAINER_MODE([enable])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([autotools/m4])

AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_OPENMP
AC_USE_SYSTEM_EXTENSIONS

gl_EARLY
gl_INIT

AM_PROG_AR
LT_INIT
AC_SUBST([LIBTOOL_DEPS])

AC_CHECK_FUNCS_ONCE(m4_flatten([
       scandirat
]))

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
			  [support qmanifest applet])],
			  [], [enable_qmanifest=auto])
LIBSSL=
LIBBL2=
LIBZ=
HASGPGME=
AS_IF([test "x$enable_qmanifest" != xno],
	  [AC_CHECK_HEADERS([openssl/err.h \
	                     openssl/ssl.h], [], [LIBSSL=_missing_header])
	   AC_CHECK_LIB([ssl${LIBSSL}], [SSL_connect],
					[LIBSSL="-lssl"
					 AC_DEFINE([HAVE_SSL], [1], [Define if you have ssl])
					 AC_CHECK_LIB([crypto],
								  [ERR_reason_error_string],
								  [LIBSSL="${LIBSSL} -lcrypto"],
								  [])
					 AC_SUBST([LIBSSL], ["${LIBSSL}"])
					],
					[if test "x$enable_qmanifest" != xauto; then
					 AC_MSG_FAILURE(
					   [--enable-qmanifest was given, but test for ssl failed])
					 fi
					 LIBSSL=
					])
	   AC_CHECK_HEADERS([blake2.h], [], [LIBBL2=_missing_header])
	   AC_CHECK_LIB([b2${LIBBL2}], [blake2b_update],
					[LIBBL2="-lb2"
					 AC_DEFINE([HAVE_BLAKE2B], [1],
							   [Define if you have blake2b])
					 AC_SUBST([LIBBL2], ["${LIBBL2}"])
					],
					[if test "x$enable_qmanifest" != xauto; then
					 AC_MSG_FAILURE(
					   [--enable-qmanifest was given, but test for blake2b failed])
					 fi
					 LIBBL2=
					])
	   AC_CHECK_HEADERS([zlib.h], [], [LIBZ=_missing_header])
	   AC_CHECK_LIB([z${LIBZ}], [gzopen],
					[LIBZ="-lz"
					 AC_DEFINE([HAVE_LIBZ], [1],
							   [Define if you have zlib])
					 AC_SUBST([LIBZ], ["${LIBZ}"])
					],
					[if test "x$enable_qmanifest" != xauto; then
					 AC_MSG_FAILURE(
					   [--enable-qmanifest was given, but test for libz failed])
					 fi
					 LIBZ=
					])
	   AM_PATH_GPGME([], [HASGPGME=yes])
	   AC_MSG_CHECKING([whether to enable qmanifest])
	   case "x${LIBSSL}${LIBBL2}${LIBZ}-${HASGPGME}" in
		   "x-lssl"*"-lb2-lz-yes")
				AC_MSG_RESULT([yes])
				;;
		   *)
			   enable_qmanifest=no
			   AC_MSG_RESULT([no: missing dependencies])
			   ;;
	   esac
	   if test "x$enable_qmanifest" != xno ; then
		   AC_DEFINE([ENABLE_QMANIFEST], [1],
					 [Define if qmanifest should be compiled])
	   fi
	],
	[
	   AC_MSG_CHECKING([whether to enable qmanifest])
	   AC_MSG_RESULT([no: disabled by configure argument])
	])
AM_CONDITIONAL([QMANIFEST_ENABLED], [test "x$enable_qmanifest" != xno])

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
	tests/qmerge/Makefile
	tests/qtbz2/Makefile
	tests/quse/Makefile
	tests/qxpak/Makefile
	tests/rmspace/Makefile
	tests/source/Makefile
])
AC_OUTPUT
