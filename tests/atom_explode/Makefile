thisdir = atom_explode
include ../subdir.mk

all: $(b)/e

$(b)/e: $(s)/test.c
	mkdir -p $(b)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -o $@

test check: dotest basic

basic: $(b)/e
	$(b)/e < $(s)/basic.tests > $(b)/basic
	diff -u $(s)/basic.good $(b)/basic

dotest: $(b)/e
	$(Q)$(s)/dotest

clean:
	rm -f $(b)/me $(b)/py $(b)/e $(b)/q $(b)/basic

.PHONY: all basic dotest test check clean
