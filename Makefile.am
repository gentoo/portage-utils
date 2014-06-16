ACLOCAL_AMFLAGS = -I autotools/m4

SUBDIRS = autotools/gnulib

bin_PROGRAMS = q
q_SOURCES = main.c
q_CPPFLAGS = \
	-I$(top_builddir)/autotools/gnulib \
	-I$(top_srcdir)/autotools/gnulib
q_LDADD = \
	$(top_builddir)/autotools/gnulib/libgnu.a \
	$(LIB_CLOCK_GETTIME) \
	$(LIB_EACCESS)

install-exec-hook:
	cd $(DESTDIR)$(bindir); \
	for applet in $(APPLETS) ; do \
		[ ! -e "$$applet" ] && ln -s q $${applet} ; \
	done

TMAKE = \
	$(MAKE) -C $(abs_top_srcdir)/tests \
		AUTOTOOLS=true \
		abs_top_builddir="$(abs_top_builddir)" \
		abs_top_srcdir="$(abs_top_srcdir)"
check-hook:
	$(TMAKE) check
clean-local:
	$(TMAKE) clean
	-rmdir tests/*/ tests/
	rm -f $(APPLETS)
check: check-hook

# Start off with base values which we append below
dist_man_MANS =
APPLETS =
EXTRA_DIST = autotools/m4/gnulib-cache.m4

# @@@ GEN START @@@ #
q_CPPFLAGS += \
	-DAPPLET_q \
	-DAPPLET_qatom \
	-DAPPLET_qcache \
	-DAPPLET_qcheck \
	-DAPPLET_qdepends \
	-DAPPLET_qfile \
	-DAPPLET_qgrep \
	-DAPPLET_qlist \
	-DAPPLET_qlop \
	-DAPPLET_qmerge \
	-DAPPLET_qpkg \
	-DAPPLET_qsearch \
	-DAPPLET_qsize \
	-DAPPLET_qtbz2 \
	-DAPPLET_quse \
	-DAPPLET_qxpak
dist_man_MANS += \
	man/q.1 \
	man/qatom.1 \
	man/qcache.1 \
	man/qcheck.1 \
	man/qdepends.1 \
	man/qfile.1 \
	man/qgrep.1 \
	man/qlist.1 \
	man/qlop.1 \
	man/qmerge.1 \
	man/qpkg.1 \
	man/qsearch.1 \
	man/qsize.1 \
	man/qtbz2.1 \
	man/quse.1 \
	man/qxpak.1
APPLETS += \
	q \
	qatom \
	qcache \
	qcheck \
	qdepends \
	qfile \
	qgrep \
	qlist \
	qlop \
	qmerge \
	qpkg \
	qsearch \
	qsize \
	qtbz2 \
	quse \
	qxpak
EXTRA_DIST += \
	applets.h \
	include_applets.h \
	libq/atom_compare.c \
	libq/atom_explode.c \
	libq/basename.c \
	libq/busybox.h \
	libq/colors.c \
	libq/compat.c \
	libq/copy_file.c \
	libq/hash_fd.c \
	libq/human_readable.c \
	libq/i18n.h \
	libq/libq.c \
	libq/libq.h \
	libq/md5_sha1_sum.c \
	libq/prelink.c \
	libq/profile.c \
	libq/rmspace.c \
	libq/safe_io.c \
	libq/scandirat.c \
	libq/vdb.c \
	libq/vdb_get_next_dir.c \
	libq/virtuals.c \
	libq/xarray.c \
	libq/xasprintf.c \
	libq/xchdir.c \
	libq/xgetcwd.c \
	libq/xmalloc.c \
	libq/xmkdir.c \
	libq/xreadlink.c \
	libq/xregex.c \
	libq/xstrdup.c \
	libq/xsystem.c \
	main.c \
	main.h \
	porting.h \
	q.c \
	qatom.c \
	qcache.c \
	qcheck.c \
	qdepends.c \
	qfile.c \
	qglsa.c \
	qgrep.c \
	qlist.c \
	qlop.c \
	qmerge.c \
	qpkg.c \
	qsearch.c \
	qsize.c \
	qtbz2.c \
	quse.c \
	qxpak.c \
	tests/Makefile \
	tests/atom_compare/.gitignore \
	tests/atom_compare/Makefile \
	tests/atom_compare/atom-compare.py \
	tests/atom_compare/static.good \
	tests/atom_compare/static.q.good \
	tests/atom_compare/static.q.tests \
	tests/atom_compare/static.tests \
	tests/atom_explode/.gitignore \
	tests/atom_explode/Makefile \
	tests/atom_explode/atom-explode.py \
	tests/atom_explode/basic.good \
	tests/atom_explode/basic.tests \
	tests/atom_explode/dotest \
	tests/atom_explode/test.c \
	tests/init.sh \
	tests/install/.gitignore \
	tests/install/Makefile \
	tests/install/dotest \
	tests/mkdir/.gitignore \
	tests/mkdir/Makefile \
	tests/mkdir/dotest \
	tests/mkdir/test.c \
	tests/profile/Makefile \
	tests/profile/dotest \
	tests/profile/profile1/etc/parent.conf \
	tests/profile/profile1/etc/portage/make.conf \
	tests/profile/profile1/etc/portage/subdir/file.conf \
	tests/profile/profile1/etc/portage/this.level.conf \
	tests/qdepends/Makefile \
	tests/qdepends/dotest \
	tests/qdepends/list01.good \
	tests/qdepends/list02.good \
	tests/qdepends/list03.good \
	tests/qdepends/list04.good \
	tests/qdepends/list05.good \
	tests/qdepends/list06.good \
	tests/qdepends/list07.good \
	tests/qdepends/list08.good \
	tests/qdepends/root/app-arch/cpio-2.11/CONTENTS \
	tests/qdepends/root/app-arch/cpio-2.11/SLOT \
	tests/qdepends/root/app-arch/cpio-2.11/repository \
	tests/qdepends/root/x11-apps/xdm-1.1.11-r3/DEPEND \
	tests/qdepends/root/x11-apps/xdm-1.1.11-r3/RDEPEND \
	tests/qdepends/root/x11-apps/xdm-1.1.11-r3/USE \
	tests/qfile/Makefile \
	tests/qfile/dotest \
	tests/qlist/Makefile \
	tests/qlist/dotest \
	tests/qlist/list01.good \
	tests/qlist/list02.good \
	tests/qlist/list03.good \
	tests/qlist/list04.good \
	tests/qlist/list05.good \
	tests/qlist/list06.good \
	tests/qlist/list07.good \
	tests/qlist/list08.good \
	tests/qlist/list09.good \
	tests/qlist/list10.good \
	tests/qlist/list11.good \
	tests/qlist/list12.good \
	tests/qlist/list13.good \
	tests/qlist/list14.good \
	tests/qlist/root/-merge-foo/CONTENTS \
	tests/qlist/root/a-b/a-0/CONTENTS \
	tests/qlist/root/a-b/a-0/SLOT \
	tests/qlist/root/a-b/a-0/repository \
	tests/qlist/root/a-b/b-0/CONTENTS \
	tests/qlist/root/a-b/b-0/SLOT \
	tests/qlist/root/a-b/b-0/repository \
	tests/qlist/root/app-arch/cpio-2.11/CONTENTS \
	tests/qlist/root/app-arch/cpio-2.11/SLOT \
	tests/qlist/root/app-arch/cpio-2.11/repository \
	tests/qlist/root/cat/pkg-1/CONTENTS \
	tests/qlist/root/cat/pkg-1/SLOT \
	tests/qlist/root/cat/pkg-1/repository \
	tests/qlist/root/cat/sub-2/CONTENTS \
	tests/qlist/root/cat/sub-2/SLOT \
	tests/qlist/root/cat/sub-2/repository \
	tests/qlist/root/sys-fs/mtools-4.0.13/CONTENTS \
	tests/qlist/root/sys-fs/mtools-4.0.13/SLOT \
	tests/qlist/root/sys-fs/mtools-4.0.13/repository \
	tests/qmerge/Makefile \
	tests/qmerge/dotest \
	tests/qmerge/packages/Packages \
	tests/qmerge/packages/sys-devel/qmerge-test-1.3.tbz2 \
	tests/qtbz2/Makefile \
	tests/qtbz2/dotest \
	tests/quse/Makefile \
	tests/quse/dotest \
	tests/quse/list01.good \
	tests/quse/portdir/profiles/arch.list \
	tests/quse/portdir/profiles/desc/elibc.desc \
	tests/quse/portdir/profiles/desc/one.desc \
	tests/quse/portdir/profiles/desc/two.desc \
	tests/quse/portdir/profiles/use.desc \
	tests/quse/portdir/profiles/use.local.desc \
	tests/qxpak/Makefile \
	tests/qxpak/dotest \
	tests/qxpak/list01.good \
	tests/qxpak/list02.good \
	tests/reinitialize/Makefile \
	tests/reinitialize/dotest \
	tests/source/Makefile \
	tests/source/dotest \
	tests/source/space \
	tests/subdir.mk \
	tests/tests.h
# @@@ GEN START @@@ #
