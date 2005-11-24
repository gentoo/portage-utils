all: q

q: test.c
	$(CC) $(CFLAGS) test.c -o q

test check: q
	./dotest

clean:
	rm -f me py q
