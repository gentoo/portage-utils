/*
 * Copyright 2010-2022 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2022      Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _XSYSTEM_H
#define _XSYSTEM_H 1

void xsystembash(const char *command, const char **argv, int cwd);
#define xsystem(C,F)  xsystembash(C, NULL, F)
#define xsystemv(V,F) xsystembash(NULL, V, F)

#endif
