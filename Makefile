# Copyright 2005 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-projects/portage-utils/Makefile,v 1.29 2005/11/06 18:07:17 solar Exp $
####################################################################
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston,
# MA 02111-1307, USA.
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
#CFLAGS   += -DOPTIMIZE_FOR_SIZE
#LDFLAGS  := -pie
DESTDIR    =
PREFIX    := $(DESTDIR)/usr
STRIP     := strip
MKDIR     := mkdir -p
CP        := cp

ifdef PV
HFLAGS    += -DVERSION=\"$(PV)\"
endif

#####################################################
APPLETS    = q qfile qlist qsearch quse qsize qcheck qdepends qlop qtbz2 qxpak qpkg qgrep
SRC        = $(APPLETS:%=%.c) main.c
MPAGES     = man/q.1

all: q
	@:

debug:
	$(MAKE) CFLAGS="$(CFLAGS) -DEBUG -g3 -ggdb -fno-pie" clean symlinks
	@-/sbin/chpax  -permsx $(APPLETS)
	@-/sbin/paxctl -permsx $(APPLETS)

q: $(SRC) libq/*.c *.h libq/*.h
ifeq ($(subst s,,$(MAKEFLAGS)),$(MAKEFLAGS))
	@echo $(CC) $(CFLAGS) $(LDFLAGS) main.c -o q
endif
	@$(CC) $(WFLAGS) $(LDFLAGS) $(CFLAGS) $(HFLAGS) main.c -o q

depend:
	#$(CC) $(CFLAGS) -MM $(SRC) > .depend
	$(CC) $(CFLAGS) -MM main.c > .depend

check: symlinks
	$(MAKE) -C tests $@

clean:
	-rm -f q

distclean: clean testclean depend
	-rm -f *~ core
	-rm -f `find . -type l`
testclean:
	cd tests && $(MAKE) clean

install: all
	-$(MKDIR) $(PREFIX)/bin/ $(PREFIX)/share/man/man1/
	$(CP) q $(PREFIX)/bin/
	for mpage in $(MPAGES) ; do \
		[ -e $$mpage ] \
			&& cp $$mpage $(PREFIX)/share/man/man1/ || : ;\
	done
	(cd $(PREFIX)/bin/ && for applet in $(APPLETS); do [[ ! -e $$applet ]] && ln -s q $${applet} ; done) || :

symlinks: all
	./q --install

-include .depend
