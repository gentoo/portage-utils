LIB_PY=$(shell  python -c 'import sys;print sys.version[:3]')
LDFLAGS_PY := -lnsl -lreadline -lncurses -lieee -lpthread -lutil -lm -lpython$(LIB_PY)

LDFLAGS += $(shell echo | $(CC) -dM -E - | grep -q ' __linux__' && echo '-ldl') -Wl,--export-dynamic

SHARED_CFLAGS += -shared -fPIC -Wall

all: libhello.so libpy.so

Python.h:
	echo "#include <python$(LIB_PY)/Python.h>" > Python.h

libpy.so: Python.h py.c
	$(CC) $(SHARED_CFLAGS) $(CFLAGS) py.c $(LDFLAGS) $(LDFLAGS_PY) -o $@

libhello.so: hello.c
	$(CC) $(SHARED_CFLAGS) $(CFLAGS) hello.c $(LDFLAGS) -o $@

check test:

clean:
	rm -f *.so

distclean: clean
	rm -f *~ Python.h
