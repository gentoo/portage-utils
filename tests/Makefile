TESTS = reinitialize atom_compare atom_explode qfile quse install source

all: check

test check:
	for test in $(TESTS) ; do \
		cd $$test ; \
		$(MAKE) check || exit 1 ; \
		cd .. ; \
	done

clean:
	for test in $(TESTS) ; do \
		cd $$test ; \
		$(MAKE) clean ; \
		cd .. ; \
	done
