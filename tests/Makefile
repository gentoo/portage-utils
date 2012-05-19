TESTS = \
	reinitialize atom_compare atom_explode mkdir \
	qfile qlist qtbz2 quse qxpak \
	install profile source

all: check

test check: $(TESTS:=_subdir)

%_subdir:
	cd $(@:_subdir=) || exit 1; \
	out=`$(MAKE) check 2>&1`; \
	ret=$$?; \
	echo "$$out"; \
	exit $$ret

clean:
	for test in $(TESTS) ; do \
		$(MAKE) -C $$test clean ; \
	done
