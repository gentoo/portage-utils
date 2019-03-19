this_subdir = tests/quse
b = $(abs_top_builddir)/$(this_subdir)
s = $(abs_top_srcdir)/$(this_subdir)

all: check

test check:
	$(Q)$(s)/dotest

clean:
	rm -f $(b)/PORTDIR-* $(b)/*.squashfs

.PHONY: all check clean test
