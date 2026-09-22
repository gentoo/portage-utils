/*
 * Copyright 2011-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "rmspace.h"
#include "profile.h"

static void *
profile_walk_at
(
  int          dir_fd,
  const char  *dir,
  const char  *file,
  profile_cb_t callback,
  void        *data
)
{
  FILE  *fp;
  char  *buf;
  size_t buflen;
  int    subdir_fd;
  int    fd;
  int    linelen;

  /* Pop open this profile dir */
  subdir_fd = openat(dir_fd, dir, O_RDONLY | O_CLOEXEC | O_PATH);
  if (subdir_fd < 0)
    return data;

  /* Then open the file */
  fd = openat(subdir_fd, file, O_RDONLY | O_CLOEXEC);
  if (fd >= 0)
  {
    if ((fp = fdopen(fd, "r")) != NULL)
    {
      /* hand feed the file to the callback */
      buf = NULL;
      while (getline(&buf, &buflen, fp) != -1)
        data = callback(data, buf);
      free(buf);

      /* does close(fd) for us */
      fclose(fp);
    }
    else
    {
      close(fd);
    }
  }

  /* Now walk the parents */
  fd = openat(subdir_fd, "parent", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    goto done;
  fp = fdopen(fd, "r");
  if (!fp)
  {
    close(fd);
    goto done;
  }

  buf = NULL;
  while ((linelen = getline(&buf, &buflen, fp)) >= 0)
  {
    char *s;

    rmspace_len(buf, (size_t)linelen);

    s = strchr(buf, '#');
    if (s)
      *s = '\0';

    data = profile_walk_at(subdir_fd, buf, file, callback, data);
  }
  free(buf);

  /* does close(fd) for us */
  fclose(fp);

done:
  if (subdir_fd != AT_FDCWD)
    close(subdir_fd);

  return data;
}

void *
profile_walk_rows
(
  const char  *file,
  profile_cb_t callback,
  void        *data)
{
  /* Walk the profiles and read the file in question */
  data = profile_walk_at(AT_FDCWD,
                         CONFIG_EPREFIX "etc/make.profile",
                         file, callback, data);
  return profile_walk_at(AT_FDCWD,
                         CONFIG_EPREFIX "etc/portage/make.profile",
                         file, callback, data);
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
