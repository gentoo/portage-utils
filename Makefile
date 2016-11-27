# Copyright 2005-2014 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
####################################################################

check_gcc=$(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null > /dev/null 2>&1; \
	then echo "$(1)"; else echo "$(2)"; fi)
istrue = $(if $(filter 1 yes true on,$(strip $1)),1,0)

####################################################
WFLAGS    := -Wall -Wunused -Wimplicit -Wshadow -Wformat=2 \
             -Wmissing-declarations -Wno-missing-prototypes -Wwrite-strings \
             -Wbad-function-cast -Wnested-externs -Wcomment -Winline \
             -Wchar-subscripts -Wcast-align -Wno-format-nonliteral \
             $(call check_gcc, -Wsequence-point) \
             $(call check_gcc, -Wextra) \
             $(call check_gcc, -Wno-sign-compare) \
             $(call check_gcc, -Wold-style-definition)

CFLAGS    ?= -O2 -g -pipe
CFLAGS    += -std=gnu99
CPPFLAGS  ?=
CPPFLAGS  += -DENABLE_NLS=$(call istrue,$(NLS))
DBG_CFLAGS = -O0 -DEBUG -g3 -ggdb -fno-pie $(call check_gcc, -fsanitize=address -fsanitize=leak -fsanitize=undefined)
#CFLAGS   += -Os -DOPTIMIZE_FOR_SIZE=2 -falign-functions=2 -falign-jumps=2 -falign-labels=2 -falign-loops=2
LDFLAGS_static_1 = -static
LDFLAGS   += $(LDFLAGS_static_$(call istrue,$(STATIC)))
LIBADD    += $(shell echo | $(CC) -dM -E - | grep -q ' __FreeBSD__' && echo '-lkvm')
LIBADD    += -liniparser
DESTDIR   :=
PREFIX    := $(DESTDIR)/usr
ETCDIR    := $(DESTDIR)/etc
STRIP     := strip
MKDIR     := mkdir -p
CP        := cp
INSTALL_EXE := install -m755

ifndef V
Q = @
else
Q =
endif
export Q
ifdef PV
CPPFLAGS  += -DVERSION=\"$(PV)\"
else
PV        := git
VCSID     := $(shell git describe --tags HEAD)
CPPFLAGS  += -DVCSID='"$(VCSID)"'
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
APPLETS   := $(shell ./applets.sh)
SRC       := $(APPLETS:%=%.c) main.c
APP_FLAGS := $(foreach a,$(APPLETS),-DAPPLET_$a)
CPPFLAGS  += $(APP_FLAGS)

all: q
	@true

debug: clean
	$(MAKE) CFLAGS="$(CFLAGS) $(DBG_CFLAGS)" symlinks
	@-scanelf -o /dev/null -BXxz permsx q

q: $(SRC) libq/*.c *.h libq/*.h
ifeq ($(subst s,,$(MAKEFLAGS)),$(MAKEFLAGS))
	@printf ': %s ' $(APPLETS)
	@echo ':'
ifndef V
	@echo $(CC) $(CFLAGS) $(PYFLAGS) $(LDFLAGS) main.c -o q $(LIBADD)
endif
endif
	$(Q)$(CC) $(WFLAGS) $(PYFLAGS) $(LDFLAGS) $(CFLAGS) $(CPPFLAGS) main.c -o q $(LIBADD)

.depend: $(SRC) applets.h
	sed -n '/^DECLARE_APPLET/s:.*(\(.*\)).*:#include "\1.c":p' applets.h > include_applets.h
	@#$(CC) $(CFLAGS) -MM $(SRC) > .depend
	$(CC) $(CPPFLAGS) $(CFLAGS) -MM main.c > .depend

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
	-rm -f q $(APPLETS)
	$(MAKE) -C tests clean
distclean: clean testclean
	-rm -f *~ core .#*
	-rm -f `find . -type l`

testclean:
	cd tests && $(MAKE) clean

install: all
	$(MKDIR) $(PREFIX)/bin/
	$(INSTALL_EXE) q $(PREFIX)/bin/

	set -e ; \
	for applet in $(filter-out q,$(APPLETS)) ; do \
		ln -sf q $(PREFIX)/bin/$${applet} ; \
	done

	$(MKDIR) $(ETCDIR)/portage/repo.postsync.d
	$(INSTALL_EXE) repo.postsync/* $(ETCDIR)/portage/repo.postsync.d/

ifneq ($(wildcard man/*.1),)
	$(MKDIR) $(PREFIX)/share/man/man1/
	cp $(wildcard man/*.1) $(PREFIX)/share/man/man1/
endif

	$(MKDIR) $(PREFIX)/share/doc/$(PF)
	cp $(DOCS) $(PREFIX)/share/doc/$(PF)/

man: q
	./man/mkman.py

symlinks: q
	./q --install

-include .depend

.PHONY: all check clean debug dist distclean install man symlinks testclean

#
# All logic related to autotools is below here
#
GEN_MARK_START = \# @@@ GEN START @@@ \#
GEN_MARK_END   = \# @@@ GEN START @@@ \#
EXTRA_DIST = \
	$(SRC) \
	qglsa.c \
	$(wildcard libq/*.c *.h libq/*.h) \
	$(shell find tests -type f)
MAKE_MULTI_LINES = $(patsubst %,\\\\\n\t%,$(sort $(1)))
# 2nd level of indirection here is so the $(find) doesn't pick up
# files in EXTRA_DIST that get cleaned up ...
autotools-update: clean
	$(MAKE) _autotools-update
_autotools-update:
	sed -i '/^$(GEN_MARK_START)$$/,/^$(GEN_MARK_END)$$/d' Makefile.am
	printf '%s\nq_CPPFLAGS += %b\ndist_man_MANS += %b\nAPPLETS += %b\nEXTRA_DIST += %b\n%s\n' \
		"$(GEN_MARK_START)" \
		"$(call MAKE_MULTI_LINES,$(APP_FLAGS))" \
		"$(call MAKE_MULTI_LINES,$(wildcard man/*.1))" \
		"$(call MAKE_MULTI_LINES,$(APPLETS))" \
		"$(call MAKE_MULTI_LINES,$(EXTRA_DIST))" \
		"$(GEN_MARK_END)" \
		>> Makefile.am
autotools: autotools-update
	./autogen.sh --from=make

.PHONY: autotools autotools-update _autotools-update
