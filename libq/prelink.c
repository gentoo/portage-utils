/*
 * Copyright 2014-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include "main.h"

#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#ifdef __linux__
# include <elf.h>
# define LIBQ_PRELINK_HAS_ELF 1
#endif

#include "prelink.h"

static const char prelink_bin[] = "prelink";

static int
prelink_in_current_path
(
  bool quiet_missing
)
{
  pid_t pid;

  switch ((pid = fork()))
  {
  case -1:
    errp("error forking process");
  case 0:
    {
      /* we are the child */
      int dev_null;

      close(STDOUT_FILENO);
      dev_null = open("/dev/null", O_WRONLY);
      if (dev_null == -1)
      {
        warnp("Error opening /dev/null");
        _exit(2);
      }
      if (dup2(dev_null, STDOUT_FILENO) == -1)
      {
        warnp("Error redirecting output");
        _exit(2);
      }
      execlp(prelink_bin, prelink_bin, "--version", NULL);
      if (!quiet_missing ||
          errno != ENOENT)
        warnp("error executing %s", prelink_bin);
      _exit(errno == ENOENT ? 1 : 2);
    }
  default:
    {
      /* we are the parent */
      int status;

      waitpid(pid, &status, 0);
      if (WIFEXITED(status))
        return MIN(WEXITSTATUS(status), 2);
      else
        errp("%s freaked out %#x", prelink_bin, status);
    }
  }
}

bool
prelink_available(void)
{
  int status = prelink_in_current_path(true);

  if (status == 1)
  {
    /* extend path to include sbin and search again */
    static const char sbin_path[] = "/sbin:/usr/sbin:/usr/local/sbin";
    const char       *curpath     = getenv("PATH");
    char             *path;

    path = xasprintf("PATH=%s%s%s",
                     curpath ? curpath : "",
                     curpath ? ":"     : "",
                     sbin_path);
    putenv(path);
    status = prelink_in_current_path(false);
  }

  return status == 0 ? true : false;
}

#if LIBQ_PRELINK_HAS_ELF == 1
static bool
is_prelink_elf
(
  int         fd,
  const char *filename
)
{
  unsigned char header[EI_NIDENT + 2];
  ssize_t       len;
  uint16_t      e_type;

  len = read(fd, &header, sizeof(header));
  if (len == -1)
  {
    warnp("unable to read %s", filename);
    return false;
  }
  if (lseek(fd, 0, SEEK_SET) != 0)
    errp("unable to reset after ELF check %s\n", filename);
  if (len < (ssize_t)sizeof(header))
    return false;

  if (memcmp(header, ELFMAG, SELFMAG))
    return false;

  /* prelink only likes certain types of ELFs */
  switch (header[EI_DATA])
  {
  case ELFDATA2LSB:
    e_type = header[EI_NIDENT] | (header[EI_NIDENT + 1] << 8);
    break;
  case ELFDATA2MSB:
    e_type = header[EI_NIDENT + 1] | (header[EI_NIDENT] << 8);
    break;
  default:
    return false;
  }
  switch (e_type)
  {
  case ET_EXEC:
  case ET_DYN:
    return true;
  default:
    return false;
  }

  /* XXX: should we also check OS's/arches that prelink supports ? */
}
#else
static bool is_prelink_elf(int fd, const char *filename)
{
  (void) fd;
  (void) filename;

  return false;
}
#endif

int
prelink_undo_hash_cb
(
  int         fd,
  const char *filename
)
{
  static const char *const argv[] = {
    prelink_bin,
    "--undo",
    "--undo-output=-",
    "/dev/stdin",
    NULL,
  };
  int pipefd[2];

  if (!is_prelink_elf(fd, filename))
    return fd;

  if (pipe(pipefd))
    errp("unable to create pipe");

  switch (fork())
  {
  case -1:
    errp("error forking process");
  case 0:
    {
      /* we are the child */
      static const char *const cat_argv[] = { "cat", NULL, };
      pid_t pid;

      /* make sure we get notified of the child exit */
      signal(SIGCHLD, SIG_DFL);

      /* connect up stdin/stdout for reading/writing the file */
      close(pipefd[0]);
      if (dup2(fd, STDIN_FILENO) == -1)
      {
        warnp("error redirecting input");
        _exit(EXIT_FAILURE);
      }
      if (dup2(pipefd[1], STDOUT_FILENO) == -1)
      {
        warnp("error redirecting output");
        _exit(EXIT_FAILURE);
      }

      /* fork a monitor process ... this way the main qcheck program
       * can simply read their side of the pipe without having to wait
       * for the whole prelink program to run.  gives a bit of speed
       * up on multicore systems and doesn't need as much mem to hold
       * all the data in the pipe before we read it.  this also makes
       * it easy to fall back to `cat` when prelink skipped the file
       * that we fed it (like the split debug files). */
      switch ((pid = fork()))
      {
      case -1:
        errp("error forking process");
      case 0:
        /* we are the child */
        execvp(argv[0], (void *)argv);
        warnp("error executing %s", prelink_bin);
        _exit(1);
      default:
        {
          int status;

          waitpid(pid, &status, 0);
          if (WIFEXITED(status))
          {
            if (WEXITSTATUS(status) == EXIT_SUCCESS)
              _exit(0);
            /* assume prelink printed its own error message */
          }
          else
          {
            warnp("%s freaked out %#x", argv[0], status);
          }
          /* we've come too far!  try one last thing ... */
          execvp(cat_argv[0], (void *)cat_argv);
          _exit(1);
        }
      }
    }
  default:
    /* we are the parent */
    close(pipefd[1]);
    /* we don't need this anymore since we've got a pipe to read */
    close(fd);
    /* ignore the monitor process exit status to avoid ZOMBIES */
    signal(SIGCHLD, SIG_IGN);
    /* assume child worked ... if it didn't, it will warn for us */
    return pipefd[0];
  }

  return fd;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
