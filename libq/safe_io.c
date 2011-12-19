/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/safe_io.c,v 1.3 2011/12/19 04:41:58 vapier Exp $
 */

#include <stdio.h>

size_t safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t safe_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
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
#define fwrite safe_fwrite
