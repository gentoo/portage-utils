this_subdir = tests/copy_file
s = $(abs_top_srcdir)/$(this_subdir)

check_PROGRAMS = m
m_SOURCES = test.c
m_CPPFLAGS = -I$(top_srcdir) \
			 -I$(top_srcdir)/libq \
			 -I$(top_builddir)/autotools/gnulib \
			 -I$(top_srcdir)/autotools/gnulib
m_LDADD = $(top_builddir)/libq/libq.la \
		  $(top_builddir)/autotools/gnulib/libgnu.a \
		  $(LIB_CLOCK_GETTIME) \
		  $(LIB_EACCESS)

dotest: m
	$(s)/dotest

check-local: dotest
