# Copyright 2005-2006 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-projects/portage-utils/Makefile,v 1.49 2006/09/11 05:48:54 vapier Exp $
####################################################################

check_gcc=$(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; \
	then echo "$(1)"; else echo "$(2)"; fi)

####################################################
WFLAGS    := -Wall -Wunused -Wimplicit -Wshadow -Wformat=2 \
             -Wmissing-declarations -Wno-missing-prototypes -Wwrite-strings \
             -Wbad-function-cast -Wnested-externs -Wcomment -Winline \
             -Wchar-subscripts -Wcast-align -Wno-format-nonliteral \
             $(call check_gcc, -Wdeclaration-after-statement) \
             $(call check-gcc, -Wsequence-point) \
             $(call check-gcc, -Wextra)

CFLAGS    ?= -O2 -pipe
CFLAGS    += -funsigned-char
#CFLAGS   += -DEBUG -g
#CFLAGS   += -Os -s -DOPTIMIZE_FOR_SIZE=2
#LDFLAGS  := -pie
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

ifdef PYTHON
PYFLAGS   ?= $(shell python-config) -DWANT_PYTHON -ldl -pthread -lutil /usr/lib/libpython2.4.so
#PYFLAGS  += -lpython2.4
endif

#####################################################
APPLETS   := $(shell sed -n '/^DECLARE_APPLET/s:.*(\(.*\))$$:\1:p' applets.h|sort)
SRC       := $(APPLETS:%=%.c) main.c
HFLAGS    += $(foreach a,$(APPLETS),-DAPPLET_$a)

all: q
	@true

debug:
	$(MAKE) CFLAGS="$(CFLAGS) -DEBUG -g3 -ggdb -fno-pie" clean symlinks
	@-/usr/bin/scanelf -o /dev/null -BXxz permsx q

q: $(SRC) libq/*.c *.h libq/*.h
ifeq ($(subst s,,$(MAKEFLAGS)),$(MAKEFLAGS))
	@echo $(foreach a,$(APPLETS), : $a) :
	@echo $(CC) $(CFLAGS) $(PYFLAGS) $(LDFLAGS) main.c -o q
endif
	@$(CC) $(WFLAGS) $(PYFLAGS) $(LDFLAGS) $(CFLAGS) $(HFLAGS) main.c -o q

.depend: $(SRC)
	sed -n '/^DECLARE_APPLET/s:.*(\(.*\)).*:#include "\1.c":p' applets.h > include_applets.h
	@#$(CC) $(CFLAGS) -MM $(SRC) > .depend
	$(CC) $(HFLAGS) $(CFLAGS) -MM main.c > .depend

check: symlinks
	$(MAKE) -C tests $@

dist:
	./make-tarball.sh $(PV)

clean:
	-rm -f q
distclean: clean testclean
	-rm -f *~ core
	-rm -f `find . -type l`
testclean:
	cd tests && $(MAKE) clean

install: all
	-$(MKDIR) $(PREFIX)/bin/
	$(CP) q $(PREFIX)/bin/
	if [[ ! -d CVS ]] ; then \
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
			[[ ! -e $$applet ]] && ln -s q $${applet} ; \
		done \
	)

man: q
	cd man && ./mkman.sh

symlinks: all
	./q --install

-include .depend

.PHONY: all check clean debug dist distclean install man symlinks testclean
