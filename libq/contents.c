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
contents_parse_line_general(char *line,int line_len)
{
	static contents_entry e;
	char *p;

  if(line_len <= 0)
  {
    line_len = strlen(line);
  }

	if (line == NULL || *line == '\0' || *line == '\n')
		return NULL;

	/* chop trailing newline */
	p = &line[line_len - 1];
	if (*p == '\n')
		*p = '\0';

	memset(&e, 0x00, sizeof(e));
  e._data=line;

  if (!strncmp(e._data, "obj ", 4))
		e.type = CONTENTS_OBJ;
	else if (!strncmp(e._data, "dir ", 4))
		e.type = CONTENTS_DIR;
	else if (!strncmp(e._data, "sym ", 4))
		e.type = CONTENTS_SYM;
	else
		return NULL;

	e.name = e._data + 4;

  if(e.type == CONTENTS_DIR){
    return NULL;
  }

	/* obj /bin/bash 62ed51c8b23866777552643ec57614b0 1120707577 */
	/* sym /bin/sh -> bash 1120707577 */

  //timestamp
  for (;*p!=' ';--p) {}

  if(p == e.name){
    return NULL;
  }
  e.mtime_str=p+1;
	e.mtime = strtol(e.mtime_str, NULL, 10);
	if (e.mtime == LONG_MAX) {
    e.mtime = 0;
    e.mtime_str = NULL;
  }
  *p='\0';

  //hash
	/* obj /bin/bash 62ed51c8b23866777552643ec57614b0 */
  if(e.type == CONTENTS_OBJ){
    for (;*p!=' ';--p) {} 
    if(p == e.name){
        return NULL;
    }
    e.digest=p+1;
    *p='\0';
  }

	/* sym /bin/sh -> bash */
  if(e.type == CONTENTS_SYM){
			if ((e.sym_target = strstr(e.name, " -> ")) == NULL)
				return NULL;
			*e.sym_target = '\0';
			e.sym_target += 4;
  }
  return &e;
}
