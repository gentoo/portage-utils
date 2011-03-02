CFLAGS += -Wall

all: e

e: test.c
	$(CC) $(CFLAGS) test.c -o e

test check: dotest basic

basic: e
	./e < basic.tests > basic
	diff -u basic.good basic

dotest: e
	./dotest

clean:
	rm -f me py e

.PHONY: all basic dotest test check clean
