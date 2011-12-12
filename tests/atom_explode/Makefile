thisdir = atom_explode
include ../subdir.mk

CFLAGS += -Wall

all: $(b)/e

$(b)/e: $(s)/test.c
	mkdir -p $(b)
	$(CC) $(CFLAGS) $< -o $@

test check: dotest basic

basic: $(b)/e
	$(b)/e < $(s)/basic.tests > $(b)/basic
	diff -u $(s)/basic.good $(b)/basic

dotest: $(b)/e
	$(s)/dotest

clean:
	rm -f $(b)/me $(b)/py $(b)/e $(b)/q $(b)/basic

.PHONY: all basic dotest test check clean
