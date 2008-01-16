CFLAGS += -Wall

all: q

q: test.c
	$(CC) $(CFLAGS) test.c -o q

test check: dotest basic

basic: q
	./q < basic.tests > basic
	diff -u basic.good basic

dotest: q
	./dotest

clean:
	rm -f me py q

.PHONY: all basic dotest test check clean
