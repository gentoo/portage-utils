/*
 * Copyright 2005-2024 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _CONTENTS_H
#define _CONTENTS_H 1

#include "config.h"
#include <stddef.h>

typedef enum {
	CONTENTS_DIR, CONTENTS_OBJ, CONTENTS_SYM
} contents_type;

typedef struct {
	contents_type type;
	char *_data;
	char *name;
	char *sym_target;
	char *digest;
	char *mtime_str;
	long mtime;
} contents_entry;


contents_entry *contents_parse_line_len(char *line, size_t len);
#define contents_parse_line(L) contents_parse_line_len(L, strlen(L))
void restore_buffer_after_parsing(contents_entry *e);

#endif
