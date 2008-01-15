all:

libq:
	$(CC) -shared -fPIC $(CFLAGS) -o libq.so.0 libq.c -Dargv0=\"libq\" -D_LIB_Q

%:
	$(MAKE) -C .. $@

.PHONY: all libq
