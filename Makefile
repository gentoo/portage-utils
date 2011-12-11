# Copyright 2005-2008 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-projects/portage-utils/Makefile,v 1.71 2011/12/11 21:38:41 vapier Exp $
####################################################################

check_gcc=$(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; \
	then echo "$(1)"; else echo "$(2)"; fi)

####################################################
WFLAGS    := -Wall -Wunused -Wimplicit -Wshadow -Wformat=2 \
             -Wmissing-declarations -Wno-missing-prototypes -Wwrite-strings \
             -Wbad-function-cast -Wnested-externs -Wcomment -Winline \
             -Wchar-subscripts -Wcast-align -Wno-format-nonliteral \
             $(call check-gcc, -Wsequence-point) \
             $(call check-gcc, -Wextra)

CFLAGS    ?= -O2 -g -pipe
CFLAGS    += -std=gnu99
#CFLAGS   += -DEBUG -g
#CFLAGS   += -Os -DOPTIMIZE_FOR_SIZE=2 -falign-functions=2 -falign-jumps=2 -falign-labels=2 -falign-loops=2
#LDFLAGS  := -pie
LIBADD    += $(shell echo | $(CC) -dM -E - | grep -q ' __FreeBSD__' && echo '-lkvm')
DESTDIR   :=
PREFIX    := $(DESTDIR)/usr
STRIP     := strip
MKDIR     := mkdir -p
CP        := cp

ifdef PV
HFLAGS    += -DVERSION=\"$(PV)\"
else
PV        := cvs
endif
ifndef PF
PF        := portage-utils-$(PV)
endif
DOCS      := TODO README qsync

#ifdef PYTHON
#PYFLAGS   ?= $(shell python-config) -DWANT_PYTHON -ldl -pthread -lutil /usr/lib/libpython2.4.so
##PYFLAGS  += -lpython2.4
#endif

#####################################################
APPLETS   := $(shell sed -n '/^DECLARE_APPLET/s:.*(\(.*\))$$:\1:p' applets.h|sort)
SRC       := $(APPLETS:%=%.c) main.c
HFLAGS    += $(foreach a,$(APPLETS),-DAPPLET_$a)

all: q
	@true

debug:
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -DEBUG -g3 -ggdb -fno-pie" clean symlinks
	@-scanelf -o /dev/null -BXxz permsx q

q: $(SRC) libq/*.c *.h libq/*.h
ifeq ($(subst s,,$(MAKEFLAGS)),$(MAKEFLAGS))
	@echo $(foreach a,$(APPLETS), : $a) :
ifndef V
	@echo $(CC) $(CFLAGS) $(PYFLAGS) $(LDFLAGS) main.c -o q $(LIBADD)
endif
endif
ifndef V
	@$(CC) $(WFLAGS) $(PYFLAGS) $(LDFLAGS) $(CFLAGS) $(HFLAGS) main.c -o q $(LIBADD)
else
	$(CC) $(WFLAGS) $(PYFLAGS) $(LDFLAGS) $(CFLAGS) $(HFLAGS) main.c -o q $(LIBADD)
endif

.depend: $(SRC)
	sed -n '/^DECLARE_APPLET/s:.*(\(.*\)).*:#include "\1.c":p' applets.h > include_applets.h
	@#$(CC) $(CFLAGS) -MM $(SRC) > .depend
	$(CC) $(HFLAGS) $(CFLAGS) -MM main.c > .depend

check: symlinks
	$(MAKE) -C tests $@

dist:
	./make-tarball.sh $(PV)
distcheck: dist
	rm -rf portage-utils-$(PV)
	tar xf portage-utils-$(PV).tar.xz
	$(MAKE) -C portage-utils-$(PV)
	$(MAKE) -C portage-utils-$(PV) check
	rm -rf portage-utils-$(PV)

clean:
	-rm -f q
distclean: clean testclean
	-rm -f *~ core .#*
	-rm -f `find . -type l`

testclean:
	cd tests && $(MAKE) clean

install: all
	-$(MKDIR) $(PREFIX)/bin/
	$(CP) q $(PREFIX)/bin/
	if [ ! -d CVS ] ; then \
		$(MKDIR) $(PREFIX)/share/man/man1/ ; \
		for mpage in $(wildcard man/*.1) ; do \
			[ -e $$mpage ] \
				&& cp $$mpage $(PREFIX)/share/man/man1/ || : ;\
		done ; \
		$(MKDIR) $(PREFIX)/share/doc/$(PF) ; \
		for doc in $(DOCS) ; do \
			cp $$doc $(PREFIX)/share/doc/$(PF)/ ; \
		done ; \
	fi
	(cd $(PREFIX)/bin/ ; \
		for applet in $(APPLETS); do \
			[ ! -e "$$applet" ] && ln -s q $${applet} ; \
		done \
	)

man: q
	cd man && ./mkman.sh

symlinks: all
	./q --install

-include .depend

.PHONY: all autotools check clean debug dist distclean install man symlinks testclean

#
# All logic related to autotools is below here
#
GEN_MARK_START = \# @@@ GEN START @@@ \#
GEN_MARK_END   = \# @@@ GEN START @@@ \#
EXTRA_DIST = \
	$(SRC) \
	qglsa.c \
	$(wildcard libq/*.c *.h libq/*.h) \
	$(shell find tests -type f '!' -ipath '*/CVS/*')
MAKE_MULTI_LINES = $(patsubst %,\\\\\n\t%,$(sort $(1)))
autotools-update:
	sed -i '/^$(GEN_MARK_START)$$/,/^$(GEN_MARK_END)$$/d' Makefile.am
	printf '%s\ndist_man_MANS += %b\nAPPLETS += %b\nEXTRA_DIST += %b\n%s\n' \
		"$(GEN_MARK_START)" \
		"$(call MAKE_MULTI_LINES,$(wildcard man/*.1))" \
		"$(call MAKE_MULTI_LINES,$(APPLETS))" \
		"$(call MAKE_MULTI_LINES,$(EXTRA_DIST))" \
		"$(GEN_MARK_END)" \
		>> Makefile.am
autotools: autotools-update
	./autogen.sh --from=make
