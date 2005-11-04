TESTS = reinitialize atom_explode qfile quse

all: check

../q:
	cd ../ && $(MAKE) symlinks 

test check: ../q
	for test in $(TESTS) ; do (cd $$test && $(MAKE) all test && cd ..); done || exit 1

clean:
	for test in $(TESTS) ; do (cd $$test && $(MAKE) clean && cd ..); done || exit 1
