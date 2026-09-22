/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <xalloc.h>

#include "rmspace.h"

/* remove leading/trailing extraneous white space */
char *
rmspace_len
(
  char  *s,
  size_t len
)
{
  char *p;

  /* find the start of trailing space and set it to \0 */
  for (p = s + len - 1;
       (p >= s &&
        isspace(*p));
       p--)
    continue;
  p[1] = '\0';
  len = (p - s) + 1;

  /* find the end of leading space and set p to it */
  for (p = s;
       (isspace(*p) &&
        *p != '\0');
       p++)
    continue;

  /* move the memory backward to overwrite leading space */
  if (p != s)
    memmove(s, p, len - (p - s) + 1);

  return s;
}

char *
rmspace
(
  char *s
)
{
  return rmspace_len(s, strlen(s));
}

/* removes adjacent extraneous white space */
char *
remove_extra_space
(
  char *str
)
{
  char  *buf;
  char  *p;
  size_t len;
  size_t pos = 0;
  char   c   = ' ';

  if (str == NULL)
    return NULL;

  len = strlen(str);
  buf = xmalloc(len + 1);

  for (p = str; *p != 0; p++)
  {
    if (!isspace(*p))
    {
      c = *p;
    }
    else
    {
      if (c == ' ')
        continue;
      c = ' ';
    }
    buf[pos] = c;
    pos++;
  }
  if (pos > 0 &&
      buf[pos - 1] == ' ')
    pos--;
  buf[pos] = '\0';

  strcpy(str, buf);
  free(buf);
  return str;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
