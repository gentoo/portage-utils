thisdir = atom_compare
include ../subdir.mk

all: test

test check: static static.q portage
	diff -u $(s)/static.good $(b)/static
	diff -u $(s)/static.q.good $(b)/static.q
	diff -u $(s)/static.good $(b)/portage

t = \
	set -f && \
	$(call mkdir,$(b)) && \
	$(realpath $(lastword $^)) $(1) `sed 's:\#.*::' $<` \
	> $(@:.tests=)

static: $(s)/static.tests $(atb)/q
	$(call t,atom -c)

static.q: $(s)/static.q.tests $(atb)/q
	$(call t,atom -c)

portage: $(s)/static.tests $(s)/atom-compare.py
	$(call t)

clean:
	rm -f $(b)/static $(b)/static.q $(b)/portage

.PHONY: all check clean test
