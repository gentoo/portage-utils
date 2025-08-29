ACLOCAL_AMFLAGS = -I autotools/m4

SUBDIRS = \
	autotools/gnulib \
	libq \
	. \
	tests \
	$(NULL)

APPLETS = \
	q \
	qatom \
	qcheck \
	qdepends \
	qfile \
	qgrep \
	qkeyword \
	qlist \
	qlop \
	qmerge \
	qpkg \
	qsearch \
	qsize \
	qtbz2 \
	qtegrity \
	quse \
	qwhich \
	qxpak \
	$(NULL)

dist_man_MANS = \
	man/q.1 \
	man/qatom.1 \
	man/qcheck.1 \
	man/qdepends.1 \
	man/qfile.1 \
	man/qgrep.1 \
	man/qkeyword.1 \
	man/qlist.1 \
	man/qlop.1 \
	man/qmerge.1 \
	man/qpkg.1 \
	man/qsearch.1 \
	man/qsize.1 \
	man/qtbz2.1 \
	man/qtegrity.1 \
	man/quse.1 \
	man/qwhich.1 \
	man/qxpak.1 \
	$(NULL)

bin_PROGRAMS = q
q_SOURCES = \
	main.c \
	q.c \
	qatom.c \
	qcheck.c \
	qdepends.c \
	qfile.c \
	qgrep.c \
	qkeyword.c \
	qlist.c \
	qlop.c \
	qmerge.c \
	qpkg.c \
	qsearch.c \
	qsize.c \
	qtbz2.c \
	qtegrity.c \
	quse.c \
	qwhich.c \
	qxpak.c \
	$(NULL)
q_CPPFLAGS = \
	-I$(top_srcdir)/libq \
	-I$(top_builddir)/autotools/gnulib \
	-I$(top_srcdir)/autotools/gnulib \
	$(OPENMP_CFLAGS) \
	$(LIBBL2_CFLAGS) \
	$(LIBZ_CFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(CWFLAGS) \
	$(NULL)
q_LDADD = \
	$(top_builddir)/libq/libq.a \
	$(top_builddir)/autotools/gnulib/libgnu.a \
	$(OPENMP_CFLAGS) \
	$(LIBBL2_LIBS) \
	$(LIBZ_LIBS) \
	$(GPGME_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(LIB_CRYPTO) \
	$(LIB_CLOCK_GETTIME) \
	$(LIB_EACCESS) \
	$(SENDFILE_LIBS) \
	$(NULL)

if QMANIFEST_ENABLED
q_SOURCES += qmanifest.c
q_CPPFLAGS += \
	$(GPGME_CFLAGS)
dist_man_MANS += man/qmanifest.1
APPLETS += qmanifest
endif


install-exec-hook:
	cd $(DESTDIR)$(bindir); \
	for applet in $(APPLETS) ; do \
		[ ! -e "$${applet}" ] && $(LN_S) q $${applet} ; \
	done

clean-local:
	rm -f $(APPLETS)

EXTRA_DIST = \
	autotools/m4/gnulib-cache.m4 \
	applets.h \
	tests \
	$(NULL)
