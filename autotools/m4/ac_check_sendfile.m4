AC_DEFUN([AC_CHECK_SENDFILE],[

saved_LIBS="$LIBS"
saved_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror-implicit-function-declaration"

dnl platforms like Solaris need libsendfile, first check if it's there
AC_CHECK_LIB(sendfile, sendfile,
             [
                 LIBS="-lsendfile $LIBS"
                 SENDFILE_LIBS="-lsendfile"
                 AC_SUBST(SENDFILE_LIBS)
             ], [])

ac_sendfile_supported=no
AC_MSG_CHECKING([whether sendfile() is supported and what prototype it has])

dnl Linux/Solaris
AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/sendfile.h>
                                  #include <stdio.h>]],
                                [[sendfile(1, 1, NULL, 0);]])],
    [
        AC_DEFINE(HAVE_SENDFILE4_SUPPORT, 1,
                  [Define this if Linux/Solaris sendfile() is supported])
        AC_MSG_RESULT([Linux/Solaris sendfile()])
        ac_sendfile_supported=yes
    ], [])

dnl FreeBSD-like
if test x$ac_sendfile_supported = xno; then
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/socket.h>
                                      #include <stdio.h>]],
                                    [[sendfile(1, 1, 0, 0, NULL, NULL, 0);]])],
    [
        AC_DEFINE(HAVE_SENDFILE7_SUPPORT, 1,
                  [Define this if FreeBSD sendfile() is supported])
        AC_MSG_RESULT([FreeBSD sendfile()])
        ac_sendfile_supported=yes
    ], [])
fi

dnl macOS-like
if test x$ac_sendfile_supported = xno; then
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/socket.h>
                                      #include <stdio.h>
                                      #include <sys/uio.h>]],
                                    [[sendfile(1, 1, 0, NULL, NULL, 0);]])],
    [
        AC_DEFINE(HAVE_SENDFILE6_SUPPORT, 1,
                  [Define this if MacOS sendfile() is supported])
        AC_MSG_RESULT([MacOS sendfile()])
        ac_sendfile_supported=yes
    ], [])
fi

if test x$ac_sendfile_supported = xno; then
    AC_MSG_RESULT([no sendfile() support, using read/send])
fi

CFLAGS="$saved_CFLAGS"
LIBS="$saved_LIBS"

])
