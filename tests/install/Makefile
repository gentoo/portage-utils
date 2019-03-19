this_subdir = tests/install
b = $(abs_top_builddir)/$(this_subdir)
s = $(abs_top_srcdir)/$(this_subdir)

all: check

test check:
	$(Q)$(s)/dotest

clean:
	rm -f $(b)/q `find $(b) -type l`

.PHONY: all check clean test
