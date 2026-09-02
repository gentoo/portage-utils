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

#ifdef HAVE_CURL
# include <curl/curl.h>
#endif

#include "xalloc.h"
#include "eat_file.h"

ssize_t
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

  /* First figure out how much data we should read from the fd. */
  if (fd == -1 ||
      fstat(fd, &s) != 0)
  {
    return -1;
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
        return -1;
      buf[read_size] = '\0';
    }
    else
    {
      if ((read_size = read(fd, buf, read_size)) <= 0)
        return -1;
      buf[read_size] = '\0';
    }
  }

  return (ssize_t)read_size;
}

ssize_t
eat_file_at
(
  int         dfd,
  const char *file,
  char      **bufptr,
  size_t     *bufsize
)
{
  ssize_t ret;
  int     fd;

  fd  = openat(dfd, file, O_CLOEXEC|O_RDONLY);
  ret = eat_file_fd(fd, bufptr, bufsize);
  if (fd != -1)
    close(fd);

  return ret;
}

ssize_t
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

#ifdef HAVE_CURL
/* passing structure for cURL callback */
struct eat_file_buffer {
    char  *data;
    size_t size;
    size_t pos;
};
typedef struct eat_file_buffer eat_file_buffer;

/* cURL write callback */
static size_t
eat_file_write_cb
(
  void  *contents,
  size_t size,
  size_t nmemb,
  void  *userp
)
{
  eat_file_buffer *buf = userp;
  size_t           n   = size * nmemb;

  if (buf->size - buf->pos < n)
  {
    buf->size += n - (buf->size - buf->pos);
    buf->data = xrealloc(buf->data, buf->size + 1);
  }

  memcpy(buf->data + buf->pos, contents, n);
  buf->pos += n;
  buf->data[buf->pos] = '\0';

  return n;
}
#endif

ssize_t
eat_file_url
(
  const char *url,
  char      **bufptr,
  size_t     *bufsize
)
{
#ifndef HAVE_CURL
  (void)url;
  (void)bufptr;
  (void)bufsize;

  return -1;
#else
  CURL           *curl = curl_easy_init();
  eat_file_buffer buf;
  long            status;

  if (curl == NULL)
    return -1;

  if (*bufptr == NULL)
    *bufsize = 0;

  VAL_CLEAR(buf);
  buf.data = *bufptr;
  buf.size = *bufsize;

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, eat_file_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  if (curl_easy_perform(curl) != CURLE_OK ||
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
      status < 200 ||
      status >= 300)
  {
    curl_easy_cleanup(curl);
    return -1;
  }

  curl_easy_cleanup(curl);

  *bufptr  = buf.data;
  *bufsize = buf.size;
  return buf.pos;
#endif
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
