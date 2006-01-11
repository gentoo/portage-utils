all: test

test check: static portage
	diff -u static.good static
	diff -u static.good portage

static: static.tests ../../q
	../../q atom -c `cat static.tests` > static

portage: static.tests atom-compare.py
	./atom-compare.py `cat static.tests` > portage

clean:
	rm -f static portage
