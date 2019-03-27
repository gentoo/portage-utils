/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"
#include "safe_io.h"
#include "copy_file.h"

int copy_file_fd(int fd_src, int fd_dst)
{
	ssize_t rcnt, wcnt;
	char buf[64 * 1024];

	while (1) {
		rcnt = safe_read(fd_src, buf, sizeof(buf));
		if (rcnt < 0)
			return -1;
		else if (rcnt == 0)
			return 0;

		wcnt = safe_write(fd_dst, buf, rcnt);
		if (wcnt == -1)
			return -1;
	}
}

int copy_file(FILE *src, FILE *dst)
{
	ssize_t rcnt, wcnt;
	char buf[64 * 1024];

	while (1) {
		rcnt = fread(buf, 1, sizeof(buf), src);
		if (rcnt < 0)
			return -1;
		else if (rcnt == 0)
			return 0;

		wcnt = fwrite(buf, 1, rcnt, dst);
		if (wcnt == -1 || wcnt != rcnt)
			return -1;
	}
}
