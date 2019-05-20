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
	qmanifest \
	qmerge \
	qpkg \
	qsearch \
	qsize \
	qtbz2 \
	qtegrity \
	quse \
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
	man/qmanifest.1 \
	man/qmerge.1 \
	man/qpkg.1 \
	man/qsearch.1 \
	man/qsize.1 \
	man/qtbz2.1 \
	man/qtegrity.1 \
	man/quse.1 \
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
	qmanifest.c \
	qmerge.c \
	qpkg.c \
	qsearch.c \
	qsize.c \
	qtbz2.c \
	qtegrity.c \
	quse.c \
	qxpak.c \
	$(NULL)
q_CPPFLAGS = \
	$(OPENMP_CFLAGS) \
	$(GPGME_CFLAGS) \
	-I$(top_srcdir)/libq \
	-I$(top_builddir)/autotools/gnulib \
	-I$(top_srcdir)/autotools/gnulib \
	$(NULL)
q_LDADD = \
	$(OPENMP_CFLAGS) \
	$(top_builddir)/libq/libq.la \
	$(top_builddir)/autotools/gnulib/libgnu.a \
	-liniparser \
	$(LIBSSL) \
	$(LIBBL2) \
	$(LIBZ) \
	$(GPGME_LIBS) \
	$(LIB_CLOCK_GETTIME) \
	$(LIB_EACCESS) \
	$(NULL)

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
