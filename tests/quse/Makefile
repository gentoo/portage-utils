thisdir = quse
include ../subdir.mk

all: check

test check:
	$(Q)$(s)/dotest

clean:
	rm -f $(b)/PORTDIR-* $(b)/*.squashfs

.PHONY: all check clean test
