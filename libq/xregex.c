/*
 * Copyright 2011-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"
#include "xregex.h"

int wregcomp(regex_t *preg, const char *regex, int cflags)
{
	int ret = regcomp(preg, regex, cflags);
	if (unlikely(ret)) {
		char errbuf[256];
		regerror(ret, preg, errbuf, sizeof(errbuf));
		warn("invalid regexp: %s -- %s\n", regex, errbuf);
	}
	return ret;
}

void xregcomp(regex_t *preg, const char *regex, int cflags)
{
	if (unlikely(wregcomp(preg, regex, cflags)))
		exit(EXIT_FAILURE);
}
