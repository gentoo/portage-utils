thisdir = qmerge
include ../subdir.mk

PKGDIR = $(srcdir)/packages
PACKAGES = $(PKGDIR)/Packages

all: test

test check: $(PACKAGES)
	$(Q)$(s)/dotest

$(PACKAGES): $(wildcard $(PKGDIR)/*/*.tbz2)
	PKGDIR=$(PKGDIR) emaint binhost -f 

clean:

.PHONY: all check clean test
