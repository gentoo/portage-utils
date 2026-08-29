/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "xalloc.h"
#include "eat_file.h"

bool
eat_file_fd
(
  int     fd,
  char  **bufptr,
  size_t *bufsize
)
{
  char       *buf;
  size_t      read_size;
  struct stat s;
  bool        ret = true;

  /* First figure out how much data we should read from the fd. */
  if (fd == -1 ||
      fstat(fd, &s) != 0)
  {
    ret = false;
    read_size = 0;
    /* Fall through so we set the first byte 0 */
  }
  else if (!s.st_size)
  {
    /* We might be trying to eat a virtual file like in /proc, so
     * read an arbitrary size that should be "enough". */
    read_size = BUFSIZE;
  }
  else
  {
    read_size = (size_t)s.st_size;
  }

  /* Now allocate enough space (at least 1 byte). */
  if (!*bufptr ||
      *bufsize < read_size)
  {
    /* We assume a min allocation size so that repeat calls don't
     * hit ugly ramp ups -- if you read a file that is 1 byte, then
     * 5 bytes, then 10 bytes, then 20 bytes, ... you'll allocate
     * constantly.  So we round up a few pages as wasting virtual
     * memory is cheap when it is unused.  */
    *bufsize = ((read_size + 1) + BUFSIZE - 1) & -BUFSIZE;
    *bufptr = xrealloc(*bufptr, *bufsize);
  }
  buf = *bufptr;

  /* Finally do the actual read. */
  buf[0] = '\0';
  if (read_size)
  {
    if (s.st_size)
    {
      if (read(fd, buf, read_size) != (ssize_t)read_size)
        return false;
      buf[read_size] = '\0';
    }
    else
    {
      if ((read_size = read(fd, buf, read_size)) <= 0)
        return false;
      buf[read_size] = '\0';
    }
  }

  return ret;
}

bool
eat_file_at
(
  int         dfd,
  const char *file,
  char      **bufptr,
  size_t     *bufsize
)
{
  int  fd;
  bool ret;

  fd = openat(dfd, file, O_CLOEXEC|O_RDONLY);
  ret = eat_file_fd(fd, bufptr, bufsize);
  if (fd != -1)
    close(fd);

  return ret;
}

bool
eat_file
(
  const char *file,
  char      **bufptr,
  size_t     *bufsize
)
{
  return eat_file_at(AT_FDCWD, file, bufptr, bufsize);
}

array *
eat_file_fd_as_array
(
	int fd
)
{
  char   buf[BUFSIZ];
  char  *p;
	FILE  *f;
	array *ret;
	size_t len;

	if ((f = fdopen(fd, "r")) == NULL)
	  return NULL;

	ret = array_new();

  while ((p = fgets(buf, sizeof(buf), f)) != NULL)
  {
    len = strlen(p);
    while (len > 0 &&
           strchr("\r\n\t ", p[len - 1]) != NULL)
      p[--len] = '\0';
    if (len > 0)
      array_append_copy(ret, p, len + 1);
  }

  fclose(f);

  return ret;
}

array *
eat_file_at_as_array
(
  int         dfd,
  const char *file
)
{
  array *ret;
  int    fd;

  fd = openat(dfd, file, O_CLOEXEC|O_RDONLY);
  ret = eat_file_fd_as_array(fd);
  if (ret == NULL)
    close(fd);

  return ret;
}

array *
eat_file_as_array
(
  const char *file
)
{
  return eat_file_at_as_array(AT_FDCWD, file);
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
