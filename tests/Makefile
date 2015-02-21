TESTS = \
	reinitialize atom_compare atom_explode mkdir \
	qdepends qfile qlist qmerge qtbz2 quse qxpak \
	install profile source

all: check

test check: $(TESTS:=_subdir)

%_subdir:
	$(Q)cd $(@:_subdir=) || exit 1; \
	out=`$(MAKE) check 2>&1`; \
	ret=$$?; \
	echo "$$out"; \
	exit $$ret

clean:
	for test in $(TESTS) ; do \
		$(MAKE) -C $$test clean ; \
	done
