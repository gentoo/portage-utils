thisdir = install
include ../subdir.mk

all: check

test check:
	$(Q)$(s)/dotest

clean:
	rm -f $(b)/q `find $(b) -type l`

.PHONY: all check clean test
