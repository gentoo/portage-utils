TESTS = \
	reinitialize atom_compare atom_explode mkdir \
	qfile qlist qtbz2 quse qxpak \
	install profile source

all: check

test check:
	ret=0 ; \
	for test in $(TESTS) ; do \
		if cd $$test ; then \
			$(MAKE) check || ret=1 ; \
			cd .. ; \
		else \
			ret=1 ; \
		fi ; \
	done ; \
	exit $${ret}

clean:
	for test in $(TESTS) ; do \
		$(MAKE) -C $$test clean ; \
	done
