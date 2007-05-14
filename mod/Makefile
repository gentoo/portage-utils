LIB_PY=$(shell  python -c 'import sys;print sys.version[:3]')
LDFLAGS_PY := -lnsl -lreadline -lncurses -lieee -lpthread -lutil -lm -lpython$(LIB_PY)  -Wl,--export-dynamic
LDFLAGS += $(shell echo | $(CC) -dM -E - | grep -q ' __linux__' && echo '-ldl')

SHARED_CFLAGS += -shared -fPIC -Wall 

all: libhello.so libpy.so

Python.h:
	echo "#include <python$(LIB_PY)/Python.h>" > Python.h

libpy.so: Python.h py.c
	$(CC) $(SHARED_CFLAGS) $(CFLAGS) $(LDFLAGS_PY) $(LDFLAGS) py.c -o $@ 

libhello.so: hello.c
	$(CC) $(SHARED_CFLAGS) $(CFLAGS) $(LDFLAGS) hello.c -o $@

check test:

clean:
	rm -f *.so

distclean: clean
	rm -f *~ Python.h
