
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
	human_readable.c human_readable.h \
	i18n.h \
	move_file.c move_file.h \
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
	cur_sys_pkg.c cur_sys_pkg.h \
	$(NULL)

if !QMANIFEST_ENABLED
if !QTEGRITY_ENABLED
QFILES += hash_md5_sha1.c hash_md5_sha1.h
endif
endif

noinst_LIBRARIES = libq.a
libq_a_SOURCES = $(QFILES)
libq_a_CPPFLAGS = \
	$(OPENMP_CFLAGS) \
	-I$(top_builddir)/autotools/gnulib \
	-I$(top_srcdir)/autotools/gnulib
