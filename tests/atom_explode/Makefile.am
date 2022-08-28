this_subdir = tests/atom_explode
s = $(abs_top_srcdir)/$(this_subdir)

check_PROGRAMS = e
e_SOURCES = test.c
e_CPPFLAGS = -I$(top_srcdir) \
			 -I$(top_srcdir)/libq \
			 -I$(top_builddir)/autotools/gnulib \
			 -I$(top_srcdir)/autotools/gnulib
e_LDADD = $(top_builddir)/libq/libq.a \
		  $(top_builddir)/autotools/gnulib/libgnu.a \
		  $(LIB_CLOCK_GETTIME) \
		  $(LIB_EACCESS)

basic: e
	./e < $(s)/basic.tests > basic
	diff -u $(s)/basic.good basic

dotest: e
	$(s)/dotest

check-local: dotest basic

clean-local:
	rm -f me py q basic
