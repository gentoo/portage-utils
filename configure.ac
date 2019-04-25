AC_PREREQ([2.65])
AC_INIT([portage-utils], [git])
AM_INIT_AUTOMAKE([1.11 dist-xz no-dist-gzip silent-rules -Wall])
AM_SILENT_RULES([yes]) # AM_INIT_AUTOMAKE([silent-rules]) is broken atm
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([autotools/m4])

AC_PROG_CC_C99
AM_PROG_CC_C_O
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
