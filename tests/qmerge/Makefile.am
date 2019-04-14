this_subdir = tests/qmerge
s = $(abs_top_srcdir)/$(this_subdir)

PKGDIR = $(s)/packages
PACKAGES = $(PKGDIR)/Packages

all: test

test check: $(PACKAGES)
	$(Q)$(s)/dotest

# Ignore failures to pass tests on non-Gentoo hosts.
# Hopefully we don't hit real failures on Gentoo systems ...
$(PACKAGES): $(wildcard $(PKGDIR)/*/*.tbz2)
	PKGDIR=$(PKGDIR) emaint binhost -f || :

clean:

.PHONY: all check clean test
