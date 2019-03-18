/*
 * Copyright 2010-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _XSYSTEM_H
#define _XSYSTEM_H 1

void xsystem(const char *command);
void xsystembash(const char *command, int cwd);

#endif
