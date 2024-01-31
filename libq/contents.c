/*
 * Copyright 2005-2024 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "contents.h"

/*
 * Parse a line of CONTENTS file and provide access to the individual fields
 */
contents_entry *
contents_parse_line_len(char *line, size_t len)
{
	static contents_entry e;
	char *p;

	if (len == 0 || line == NULL || *line == '\0' || *line == '\n')
		return NULL;

	/* chop trailing newline */
	p = &line[len - 1];
	if (*p == '\n') {
		*p = '\0';
		len--;
	}

	memset(&e, 0x00, sizeof(e));
	e._data = line;

	if (!strncmp(e._data, "obj ", 4))
		e.type = CONTENTS_OBJ;
	else if (!strncmp(e._data, "dir ", 4))
		e.type = CONTENTS_DIR;
	else if (!strncmp(e._data, "sym ", 4))
		e.type = CONTENTS_SYM;
	else
		return NULL;

	e.name = e._data + 4;

	switch (e.type) {
		/* dir /bin */
		case CONTENTS_DIR:
			break;

		/* obj /bin/bash 62ed51c8b23866777552643ec57614b0 1120707577 */
		case CONTENTS_OBJ:
			for (p = &e.name[len - 1]; p >= e.name; p--) {
				if (*p == ' ') {
					if (e.mtime_str == NULL)
						e.mtime_str = p + 1;
					else if (e.digest == NULL)
						e.digest = p + 1;
					*p = '\0';

					if (e.digest != NULL)
						break;
				}
			}
			break;

		/* sym /bin/sh -> bash 1120707577 */
		case CONTENTS_SYM:
			for (p = &e.name[len - 1]; p >= e.name; p--) {
				if (*p == ' ') {
					if (e.mtime_str == NULL) {
						e.mtime_str = p + 1;
					} else if (e.sym_target == NULL) {
						if (strncmp(p, " -> ", sizeof(" -> ") - 1) == 0)
							e.sym_target = p + sizeof(" -> ") - 1;
						else
							continue;
					}
					*p = '\0';

					if (e.sym_target != NULL)
						break;
				}
			}
			break;
	}

	if (e.mtime_str) {
		e.mtime = strtol(e.mtime_str, NULL, 10);
		if (e.mtime == LONG_MAX) {
			e.mtime = 0;
			e.mtime_str = NULL;
		}
	}

	return &e;
}
