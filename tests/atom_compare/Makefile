thisdir = atom_compare
include ../subdir.mk

all: test

test check: static portage
	diff -u $(s)/static.good $(b)/static
	diff -u $(s)/static.good $(b)/portage

static: $(s)/static.tests $(atb)/q
	mkdir -p $(b)
	$(atb)/q atom -c `cat $<` > $(b)/static
	#q atom -c `cat static.tests` > static

portage: $(s)/static.tests $(s)/atom-compare.py
	mkdir -p $(b)
	./atom-compare.py `cat $<` > $(b)/portage

clean:
	rm -f $(b)/static $(b)/portage

.PHONY: all check clean test
