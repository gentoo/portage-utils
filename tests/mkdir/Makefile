CFLAGS += -Wall

all: m

m: test.c
	$(CC) $(CFLAGS) test.c -o m

test check: dotest

dotest: m
	./dotest

clean:
	rm -f m

.PHONY: all basic dotest test check clean
