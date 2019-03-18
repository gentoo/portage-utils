/*
 * "safe" versions of read/write, dealing with interrupts
 *
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include <stdio.h>
#include <errno.h>

#include "main.h"
#include "safe_io.h"

size_t
safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t ret = 0, this_ret;

	do {
		this_ret = fwrite(ptr, size, nmemb, stream);
		if (this_ret == nmemb)
			return this_ret; /* most likely behavior */
		if (this_ret == 0) {
			if (feof(stream))
				break;
			if (ferror(stream)) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				errp("fwrite(%p, %zu, %zu) failed (wrote %zu elements)",
					ptr, size, nmemb, ret);
			}
		}
		nmemb -= this_ret;
		ret += this_ret;
		ptr += (this_ret * size);
	} while (nmemb);

	return ret;
}

ssize_t safe_read(int fd, void *buf, size_t len)
{
	ssize_t ret;

	while (1) {
		ret = read(fd, buf, len);
		if (ret >= 0)
			break;
		else if (errno != EINTR)
			break;
	}

	return ret;
}

ssize_t safe_write(int fd, const void *buf, size_t len)
{
	ssize_t ret = 0;

	while (len) {
		ret = write(fd, buf, len);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += ret;
		len -= ret;
	}

	return ret;
}
