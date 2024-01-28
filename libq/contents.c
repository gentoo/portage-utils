/*
 * Copyright 2005-2020 Gentoo Foundation
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
contents_parse_line(char *line)
{
	static contents_entry e;
	char *p;

	if (line == NULL || *line == '\0' || *line == '\n')
		return NULL;

	/* chop trailing newline */
	p = &line[strlen(line) - 1];
	if (*p == '\n')
		*p = '\0';

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
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.digest = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.digest++ = '\0';
			break;

		/* sym /bin/sh -> bash 1120707577 */
		case CONTENTS_SYM:
			if ((e.mtime_str = strrchr(e.name, ' ')) == NULL)
				return NULL;
			*e.mtime_str++ = '\0';
			if ((e.sym_target = strstr(e.name, " -> ")) == NULL)
				return NULL;
			*e.sym_target = '\0';
			e.sym_target += 4;
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

void restore_buffer_after_parsing(contents_entry *e)
{
    if(!e){
        return;
    }
    switch (e->type) {
        case CONTENTS_SYM:
            *(e->sym_target-4) =' ';
            *(e->mtime_str -1) =' ';
            break;
        case CONTENTS_OBJ:
            *(e->digest-1)=' ';
            *(e->mtime_str-1)=' ';
             break;
        case CONTENTS_DIR:
            break;
    }

}
