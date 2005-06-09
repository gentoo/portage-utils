# Copyright 2005 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-projects/portage-utils/Makefile,v 1.8 2005/06/09 17:38:18 solar Exp $
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

####################################################
WFLAGS    := -Wall -Wextra -Wunused -Wimplicit -Wshadow -Wformat=2 \
             -Wmissing-declarations -Wmissing-prototypes -Wwrite-strings \
             -Wbad-function-cast -Wnested-externs -Wcomment -Wsequence-point \
             -Wdeclaration-after-statement -Wchar-subscripts -Wcast-align \
             -Winline -Wno-format-nonliteral
CFLAGS    := -O2 -pipe
#CFLAGS   += -DEBUG -g
#LDFLAGS  := -pie
DESTDIR    =
PREFIX    := $(DESTDIR)/usr
STRIP     := strip
MKDIR     := mkdir -p
CP        := cp

# Build with -Werror while emerging
ifneq ($(S),)
CFLAGS	+= -Werror
else
CFLAGS	+= $(shell [[ `hostname` == simple ]] && echo '-g3 -ggdb -nopie')
endif
#####################################################
APPLETS    = q qfile qlist qsearch quse qsize qcheck
SRC        = $(APPLETS:%=%.c) main.c
MPAGES     = man/q.1

all: q
	@:

debug: all symlinks
	@-/sbin/chpax  -permsx $(APPLETS)
	@-/sbin/paxctl -permsx $(APPLETS)

q: $(SRC)
	@echo $(CC) $(CFLAGS) $(LDFLAGS) main.c -o q
	@$(CC) $(CFLAGS) $(LDFLAGS) $(WFLAGS) main.c -o q

depend:
	$(CC) $(CFLAGS) -MM $(SOURCES) > .depend

clean:
	-rm -f q

distclean: clean
	-rm -f *~ core
	-rm -f `find . -type l`

install: all
	-$(MKDIR) $(PREFIX)/bin/ $(PREFIX)/share/man/man1/
	$(CP) q $(PREFIX)/bin/
	for mpage in $(MPAGES) ; do \
		[ -e $$mpage ] \
			&& cp $$mpage $(PREFIX)/share/man/man1/ || : ;\
	done

symlinks: all
	./q --install

-include .depend
