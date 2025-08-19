/*
 * Copyright 2005-2025 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2021-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "safe_io.h"
#include "copy_file.h"

/* includes for when sendfile is available */
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(HAVE_SENDFILE4_SUPPORT)
/* Linux/Solaris */
# include <sys/sendfile.h>
#elif defined(HAVE_SENDFILE6_SUPPORT) || defined(HAVE_SENDFILE7_SUPPORT)
/* macOS (since Darwin 9) + FreeBSD */
# include <sys/socket.h>
# include <sys/uio.h>
#endif

int copy_file_fd(int fd_src, int fd_dst)
{
#if defined(HAVE_SENDFILE4_SUPPORT) || \
	defined(HAVE_SENDFILE6_SUPPORT) || \
	defined(HAVE_SENDFILE7_SUPPORT)
	struct stat stat_buf;
	ssize_t     ret;
	size_t      len;
	off_t       offset = 0;

	if (fstat(fd_src, &stat_buf) != -1) {
		len = (size_t)stat_buf.st_size;

#if defined(HAVE_SENDFILE4_SUPPORT)
		/* Linux/Solaris */
		ret = sendfile(fd_dst, fd_src, &offset, len);
		/* everything looks fine, return success */
		if (ret == (ssize_t)len)
			return 0;
#elif defined(HAVE_SENDFILE6_SUPPORT)
		/* macOS (since Darwin 9) */
		offset = len;
		ret = (ssize_t)sendfile(fd_src, fd_dst, 0, &offset, NULL, 0);
		/* everything looks fine, return success */
		if (offset == (off_t)len)
			return 0;
#elif defined(HAVE_SENDFILE7_SUPPORT)
		/* FreeBSD */
		ret = (ssize_t)sendfile(fd_src, fd_dst, offset, len, NULL, &offset, 0);
		/* everything looks fine, return success */
		if (offset == (off_t)len)
			return 0;
#endif
		(void)ret;  /* ignore ret, we fall back */

		/* fall back to read/write, rewind the fd */
		lseek(fd_src, 0, SEEK_SET);
	}
#endif /* HAVE_SENDFILE */

	/* fallback, keep in its own scope, so we avoid 64K stack alloc if
	 * sendfile works properly */
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
