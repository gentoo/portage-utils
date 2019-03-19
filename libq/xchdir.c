/*
 * Copyright 2010-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"

#include <unistd.h>
#include <stdlib.h>

#include "xchdir.h"

void
xchdir(const char *path)
{
	if (unlikely(chdir(path) != 0))
		errp("chdir(%s) failed", path);
}
