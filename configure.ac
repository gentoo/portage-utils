AC_PREREQ([2.65])
AC_INIT([portage-utils], [cvs])
AC_CONFIG_AUX_DIR([autotools])
AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip silent-rules -Wall])
AM_SILENT_RULES([yes]) # AM_INIT_AUTOMAKE([silent-rules]) is broken atm
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([autotools/m4])

AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_USE_SYSTEM_EXTENSIONS

gl_EARLY
gl_INIT

AC_ARG_WITH([eprefix], [AS_HELP_STRING([--with-eprefix], [path for Gentoo/Prefix project])])
AC_DEFINE_UNQUOTED([EPREFIX], ["$with_eprefix"], [path for Gentoo/Prefix project])

AX_CFLAGS_WARN_ALL
AC_DEFUN([PT_CHECK_CFLAG],[AX_CHECK_COMPILER_FLAGS([$1],[CFLAGS="$CFLAGS $1"])])
m4_foreach_w([flag], [
	-Wunused
	-Wimplicit
	-Wshadow
	-Wformat=2
	-Wmissing-declarations
	-Wno-missing-prototypes
	-Wwrite-strings
	-Wbad-function-cast
	-Wnested-externs
	-Wcomment
	-Winline
	-Wchar-subscripts
	-Wcast-align
	-Wno-format-nonliteral
	-Wsequence-point
	-Wextra
], [
	AX_CHECK_COMPILER_FLAGS(flag, AS_VAR_APPEND([CFLAGS], " flag"))
])

AC_CONFIG_FILES([
	Makefile
	autotools/gnulib/Makefile
])
AC_OUTPUT
