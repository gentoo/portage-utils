/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */


/* remove leading/trailing extraneous white space */
static char *rmspace_len(char *s, size_t len)
{
	char *p;
	/* find the start of trailing space and set it to \0 */
	for (p = s + len - 1; (p >= s && isspace(*p)); --p)
		continue;
	p[1] = '\0';
	len = (p - s) + 1;
	/* find the end of leading space and set p to it */
	for (p = s; (isspace(*p) && *p); ++p)
		continue;
	/* move the memory backward to overwrite leading space */
	if (p != s)
		memmove(s, p, len - (p - s) + 1);
	return s;
}

static char *rmspace(char *s)
{
	return rmspace_len(s, strlen(s));
}
