thisdir = mkdir
include ../subdir.mk

CFLAGS += -Wall

all: $(b)/m

$(b)/m: $(s)/test.c
	mkdir -p $(b)
	$(CC) $(CFLAGS) $< -o $@

test check: dotest

dotest: $(b)/m
	$(s)/dotest

clean:
	rm -f $(b)/m

.PHONY: all basic dotest test check clean
