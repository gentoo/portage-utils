
QFILES = \
	atom.c atom.h \
	basename.c basename.h \
	busybox.h \
	colors.c colors.h \
	contents.c contents.h \
	copy_file.c copy_file.h \
	dep.c dep.h \
	eat_file.c eat_file.h \
	hash.c hash.h \
	hash_fd.c hash_fd.h \
	human_readable.c human_readable.h \
	i18n.h \
	md5_sha1_sum.c md5_sha1_sum.h \
	prelink.c prelink.h \
	profile.c profile.h \
	rmspace.c rmspace.h \
	safe_io.c safe_io.h \
	scandirat.c scandirat.h \
	set.c set.h \
	tree.c tree.h \
	xarray.c xarray.h \
	xasprintf.h \
	xchdir.c xchdir.h \
	xmkdir.c xmkdir.h \
	xpak.c xpak.h \
	xregex.c xregex.h \
	xsystem.c xsystem.h \
	$(NULL)

noinst_LTLIBRARIES = libq.la
libq_la_SOURCES = $(QFILES)
libq_la_CPPFLAGS = \
	$(OPENMP_CFLAGS) \
	-I$(top_builddir)/autotools/gnulib \
	-I$(top_srcdir)/autotools/gnulib
