ACLOCAL_AMFLAGS = -I autotools/m4

SUBDIRS = autotools/gnulib
AM_CPPFLAGS = -I$(top_builddir)/autotools/gnulib -I$(top_srcdir)/autotools/gnulib

bin_PROGRAMS = q
q_SOURCES = main.c
q_LDADD = \
	$(top_builddir)/autotools/gnulib/libgnu.a \
	$(LIB_CLOCK_GETTIME) \
	$(LIB_EACCESS)

# These lines are auto-generated, so don't touch them
dist_man_MANS = man/qatom.1 man/qpkg.1 man/qlop.1 man/qsize.1 man/qxpak.1 man/qcache.1 man/qtbz2.1 man/qsearch.1 man/qgrep.1 man/qfile.1 man/qcheck.1 man/quse.1 man/qmerge.1 man/q.1 man/qlist.1 man/qdepends.1
APPLETS = q qatom qcache qcheck qdepends qfile qgrep qlist qlop qmerge qpkg qsearch qsize qtbz2 quse qxpak
EXTRA_DIST = q.c qatom.c qcache.c qcheck.c qdepends.c qfile.c qgrep.c qlist.c qlop.c qmerge.c qpkg.c qsearch.c qsize.c qtbz2.c quse.c qxpak.c main.c qglsa.c libq/basename.c libq/rmspace.c libq/xarray.c libq/colors.c libq/atom_compare.c libq/virtuals.c libq/xasprintf.c libq/prelink.c libq/xreadlink.c libq/hash_fd.c libq/vdb_get_next_dir.c libq/profile.c libq/libq.c libq/safe_io.c libq/xchdir.c libq/compat.c libq/atom_explode.c libq/xstrdup.c libq/xmkdir.c libq/xmalloc.c libq/which.c libq/xgetcwd.c libq/human_readable.c libq/xregex.c libq/xsystem.c libq/md5_sha1_sum.c libq/copy_file.c applets.h main.h include_applets.h libq/libq.h libq/i18n.h libq/busybox.h tests/Makefile tests/quse/Makefile tests/quse/portdir/profiles/desc/one.desc tests/quse/portdir/profiles/desc/elibc.desc tests/quse/portdir/profiles/desc/two.desc tests/quse/portdir/profiles/arch.list tests/quse/portdir/profiles/use.local.desc tests/quse/portdir/profiles/use.desc tests/quse/list01.good tests/quse/dotest tests/atom_explode/Makefile tests/atom_explode/basic.good tests/atom_explode/atom-explode.py tests/atom_explode/.cvsignore tests/atom_explode/dotest tests/atom_explode/basic.tests tests/atom_explode/basic tests/atom_explode/test.c tests/install/Makefile tests/install/dotest tests/source/Makefile tests/source/space tests/source/dotest tests/source/x tests/qfile/Makefile tests/qfile/dotest tests/qxpak/Makefile tests/qxpak/list01.good tests/qxpak/dotest tests/qxpak/list02.good tests/qlist/Makefile tests/qlist/list06.good tests/qlist/list05.good tests/qlist/root/cat/pkg-1/CONTENTS tests/qlist/root/a-b/a-0/CONTENTS tests/qlist/root/a-b/b-0/CONTENTS tests/qlist/root/sys-fs/mtools-4.0.13/CONTENTS tests/qlist/root/-merge-foo/CONTENTS tests/qlist/root/app-arch/cpio-2.11/CONTENTS tests/qlist/list01.good tests/qlist/dotest tests/qlist/list03.good tests/qlist/list02.good tests/qlist/list04.good tests/reinitialize/Makefile tests/reinitialize/dotest tests/mkdir/Makefile tests/mkdir/dotest tests/mkdir/test.c tests/qtbz2/Makefile tests/qtbz2/dotest tests/init.sh tests/atom_compare/Makefile tests/atom_compare/static.tests tests/atom_compare/atom-compare.py tests/atom_compare/static.good

EXTRA_DIST += autotools/m4/gnulib-cache.m4

install-exec-hook:
	cd $(DESTDIR)$(bindir); \
	for applet in $(APPLETS) ; do \
		[ ! -e "$$applet" ] && ln -s q $${applet} ; \
	done
