/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-2018 Sam Besselink
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"
#include "libq/hash.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <xalloc.h>

#define QTEGRITY_FLAGS "a:is" COMMON_FLAGS
static struct option const qtegrity_long_opts[] = {
  {"add",                 a_argument,  NULL, 'a'},
  {"ignore-non-existent", no_argument, NULL, 'i'},
  {"show-matches",        no_argument, NULL, 's'},
  /* TODO, add this functionality
  {"convert",             a_argument,  NULL, 'c'}
   */
  COMMON_LONG_OPTS
};
static const char * const qtegrity_opts_help[] = {
  "Add file to store of known-good digests",
  "Be silent if recorded file no longer exists",
  "Show recorded digests that match with known-good digests",
  /* TODO
  "Convert known good digests to different hash function",
   */
  COMMON_OPTS_HELP
};
#define qtegrity_usage(ret) usage(ret, QTEGRITY_FLAGS, qtegrity_long_opts, qtegrity_opts_help, NULL, lookup_applet_idx("qtegrity"))

struct qtegrity_opt_state {
  char *add_file;
  bool  ima:1;
  bool  add:1;
  bool  ignore_non_exist:1;
  bool  show_matches:1;
  /* TODO
  bool  convert:1;
   */
};

#define FILE_SUCCESS  1
#define FILE_EMPTY    2
#define FILE_RELATIVE 3

#define QTEGRITY_PATH        "/var/db/QTEGRITY"
#define QTEGRITY_CUSTOM_PATH QTEGRITY_PATH "_custom"
#define QTEGRITY_IMA_PATH    "/sys/kernel/security/ima/" \
                             "ascii_runtime_measurements"
#define QTEGRITY_HASH_ALGO   "sha256"
#define QTEGRITY_HASH_LENGTH SHA256_DIGEST_LENGTH

static void
check_sha
(
  char       *ret_digest,
  const char *path,
  const char *algo
)
{
  size_t flen   = 0;
  int    hashes = 0;

  if (strcmp(algo, "sha256") == 0)
    hashes |= HASH_SHA256;
  else if (strcmp(algo, "sha512") == 0)
    hashes |= HASH_SHA512;
  else /* no matching hash? (we could support whirlpool and blake2b) */
    return;

  hash_compute_file(path, ret_digest, ret_digest, NULL, &flen, hashes);
  (void)flen;  /* we don't use the file size */

  return;
}

static void
get_fname_from_line
(
  char  *line,
  char **ret,
  int    digest_size,
  int    offset
)
{
  char  *p;
  size_t dlenstr = strlen(line);
  /* Skip first 123 chars to get to file depends on digest_func in IMA */
  size_t skip    = (((digest_size == SHA256_DIGEST_LENGTH) ||
                     (digest_size == SHA512_DIGEST_LENGTH)) ?
                    digest_size + offset + 8 : digest_size + offset + 6);

  if (dlenstr > skip)  /* assume file is at least two chars long */
    p = xstrndup(line + skip, dlenstr - skip - 1);
  else  /* e.g. digest used wrong hash algo, or malformed input */
    p = NULL;

  *ret = p;
}

static void
get_digest_from_line
(
  char *line,
  char *ret,
  int   digest_size,
  int   offset
)
{
  size_t dlenstr = strlen(line);
  /* Skip first chars to get to digest depends on digest_func in IMA */
  size_t skip    = (((digest_size == SHA256_DIGEST_LENGTH) ||
                     (digest_size == SHA512_DIGEST_LENGTH)) ?
                    offset + 8 : offset + 6);

  if (dlenstr > (digest_size + skip + 1))
  {
    memcpy(ret, line + skip, digest_size);
    ret[digest_size] = '\0';
  }
}

static void
get_known_good_digest
(
  const char *fn_store,
  char       *recorded_fname,
  char       *ret,
  int         recorded_digest_size
)
{
  FILE  *fp_store;
  char  *line;
  char  *fname;
  size_t linelen;
  int    fd_store;

  /* Open file with known good hashes */
  fd_store = open(fn_store, O_RDONLY | O_CLOEXEC, 0);
  if (fd_store == -1)
    errp("unable to open '%s'", fn_store);

  if ((fp_store = fdopen(fd_store, "r")) == NULL)
  {
    close(fd_store);
    errp("unable to fopen(%s, r)", fn_store);
  }

  /* Iterate over lines in known-good-hashes-file; per line: if fname
   * matches, grab hash. */
  line = fname = NULL;
  while (getline(&line, &linelen, fp_store) != -1)
  {
    get_fname_from_line(line, &fname, recorded_digest_size, 15);

    if (fname == NULL)
    {
      /* probably line without digest (e.g. symlink) */
      continue;
    }

    if (strcmp(recorded_fname, fname) == 0)
    {
      get_digest_from_line(line, ret, recorded_digest_size, 9);

      free(fname);
      break;
    }

    free(fname);
  }

  free(line);

  close(fd_store);
  fclose(fp_store);
}

static int
get_size_digest
(
  char *line
)
{
  char *pfound;
  int   ret    = 0;

  /* find colon; it is boundary between end of hash func & begin of
   * digest */
  pfound = strchr(line, ':');
  if (pfound != NULL)
  {
    char *line_segment;
    int   dpfound       = pfound - line;
    int   cutoff_prefix = 0;
    int   dsegment;

    if (dpfound == 55 ||
        dpfound == 6)
      ret = SHA1_DIGEST_LENGTH;
    else if (dpfound == 57)
      cutoff_prefix = 51;
    else if (dpfound == 8)
      cutoff_prefix = 0;

    /* chop off the first chars to get to the hash func */
    dsegment     = dpfound - cutoff_prefix;
    line_segment = xstrndup(line + cutoff_prefix, dsegment);

    /* If line segment equals name of hash func, then return
     * relevant const. */
    if (strcmp(line_segment, "sha512") == 0)
      ret = SHA512_DIGEST_LENGTH;
    else if (strcmp(line_segment, "sha256") == 0)
      ret = SHA256_DIGEST_LENGTH;
    else
      printf("Expected sha algo, got %s", line_segment);

    free(line_segment);
  }

  return ret;
}

static int
check_file
(
  char *filename
)
{
  if (strlen(filename) > _Q_PATH_MAX)
    err("Filename too long");

  if (filename[0] != '/')
    return FILE_RELATIVE;

  return FILE_SUCCESS;
}

int
qtegrity_main
(
  int    argc,
  char **argv
)
{
  int i;

  struct qtegrity_opt_state state = {
    .ima              = true,
    .add              = false,
    .ignore_non_exist = false,
    .show_matches     = false,
    /* TODO
    .convert          = false;
     */
  };

  while ((i = GETOPT_LONG(QTEGRITY, qtegrity, "")) != -1)
  {
    switch (i)
    {
    COMMON_GETOPTS_CASES(qtegrity)
    case 'a':
      state.ima = false;
      state.add = true;
      if (check_file(optarg) == FILE_SUCCESS)
      {
        free(state.add_file);
        state.add_file = xstrdup(optarg);
      }
      else
      {
        err("Expected absolute file as argument, got '%s'", optarg);
      }
      break;
    case 'i': state.ignore_non_exist = true; break;
    case 's': state.show_matches     = true; break;
    }
  }

  if (state.ima)
  {
    struct stat st;
    FILE       *fp_ima;
    char       *line;
    char       *recorded_fname;
    size_t      linelen;
    int         recorded_digest_size;
    int         fd_ima;


    fd_ima = open(QTEGRITY_IMA_PATH, O_RDONLY | O_CLOEXEC, 0);
    if (fd_ima == -1)
    {
      /* TODO, shouldn't we explicitly remind user IMA/securityfs
       * is needed? */
      errp("Unable to open '%s'", QTEGRITY_IMA_PATH);
    }
    if ((fp_ima = fdopen(fd_ima, "r")) == NULL)
    {
      close(fd_ima);
      errp("Unable to fopen(%s, r)", QTEGRITY_IMA_PATH);
    }

    /* Iterate over IMA file, grab fname and digest, get known good
     * digest for fname and compare */
    line = recorded_fname = NULL;
    recorded_digest_size = 0;
    while (getline(&line, &linelen, fp_ima) != -1)
    {
      char *recorded_digest;
      char *digest;

      if (line[0] != '1' ||
          line[1] != '0')
        continue;

      recorded_digest_size = get_size_digest(line);
      recorded_digest      = xmalloc(recorded_digest_size + 1);
      recorded_digest[0]   = '\0';

      /* grab fname from IMA file line */
      get_fname_from_line(line, &recorded_fname, recorded_digest_size, 51);
      /* grab digest from IMA file line, @TODO, check whether
       * digest == 000etc */
      get_digest_from_line(line, recorded_digest, recorded_digest_size, 50);

      if (recorded_fname == NULL ||
          *recorded_digest == '\0')
      {
        printf("Empty recorded filename: %s\n", line);

        if (recorded_fname != NULL)
          free(recorded_fname);

        free(recorded_digest);

        continue;
      }

      if (check_file(recorded_fname) == FILE_RELATIVE)
      {
        printf("Seems like a kernel process: %s\n", recorded_fname);

        free(recorded_fname);
        free(recorded_digest);
        continue;
      }

      if (stat(recorded_fname, &st) < 0)
      {
        if (!state.ignore_non_exist)
          printf("Couldn't access recorded file '%s'\n",
                 recorded_fname);

        free(recorded_fname);
        free(recorded_digest);
        continue;
      }

      if (!(st.st_mode & S_IXUSR ||
            st.st_mode & S_IXGRP ||
            st.st_mode & S_IXOTH))
      {
        free(recorded_fname);
        free(recorded_digest);
        continue;
      }

      digest = xmalloc(recorded_digest_size + 1);
      digest[0] = '\0';

      /* first try custom known good digests for fname */
      get_known_good_digest(QTEGRITY_CUSTOM_PATH,
                            recorded_fname, digest, recorded_digest_size);

      if (digest[0] == '\0')
      {
        /* then try from OS source */
        get_known_good_digest(QTEGRITY_PATH,
                              recorded_fname, digest, recorded_digest_size);

        if (digest[0] == '\0') {
          printf("No digest found for: %s\n", line);

          free(recorded_fname);
          free(recorded_digest);
          free(digest);
          continue;
        }
      }

      if (strcmp(recorded_digest, digest) != 0)
      {
        printf("Digest didn't match for %s\n", recorded_fname);
        printf("Known-good: '%s'...\nRecorded: '%s'\n\n",
               digest, recorded_digest);
      }
      else if (state.show_matches)
      {
        printf("Success! Digest matched for %s\n", recorded_fname);
      }

      free(recorded_fname);
      free(recorded_digest);
      free(digest);
    }

    free(line);

    close(fd_ima);
    fclose(fp_ima);
  }
  else if (state.add)
  {
    /* Add a single executable file+digest to the custom digest store */
    struct stat st;
    FILE       *fp_qtegrity_custom;
    char       *file_digest;
    char       *line;
    char       *fname;
    size_t      linelen;
    int         fd_qtegrity_custom;
    int         flush_status;
    int         recorded_digest_size = 0;
    int         skip                 = 0;

    fd_qtegrity_custom =
      open(QTEGRITY_CUSTOM_PATH, O_RDWR | O_CREAT | O_CLOEXEC, 0);
    if (fd_qtegrity_custom == -1)
      errp("Unable to open '%s'", QTEGRITY_CUSTOM_PATH);
    if ((fp_qtegrity_custom = fdopen(fd_qtegrity_custom, "w+")) == NULL)
    {
      close(fd_qtegrity_custom);
      errp("Unable to fopen(%s, r)", QTEGRITY_CUSTOM_PATH);
    }

    printf("Adding %s to %s\n", state.add_file, QTEGRITY_CUSTOM_PATH);

    if (stat(state.add_file, &st) < 0)
      errp("Couldn't access file '%s'", state.add_file);

    if (!(st.st_mode & S_IXUSR ||
          st.st_mode & S_IXGRP ||
          st.st_mode & S_IXOTH))
      err("File '%s' is not executable", state.add_file);

    /* add digest */
    file_digest = xmalloc(QTEGRITY_HASH_LENGTH + 1);
    file_digest[0] = '\0';
    check_sha(file_digest, state.add_file, QTEGRITY_HASH_ALGO);

    /* Iterate over lines; if fname matches, exit-loop */
    line = fname = NULL;
    while (getline(&line, &linelen, fp_qtegrity_custom) != -1)
    {
      recorded_digest_size = get_size_digest(line);
      get_fname_from_line(line, &fname, recorded_digest_size, 5);

      /* probably line without digest (e.g. symlink) */
      if (fname == NULL)
        continue;

      if (strcmp(state.add_file, fname) == 0)
      {
        printf("Executable already recorded, "
               "replacing digest with %s\n", file_digest);
        skip = (((recorded_digest_size == SHA256_DIGEST_LENGTH) ||
                 (recorded_digest_size == SHA512_DIGEST_LENGTH)) ?
                recorded_digest_size + 6 + 8 : recorded_digest_size + 6 + 6);
        if (fseek(fp_qtegrity_custom, -skip - strlen(fname), SEEK_CUR) == -1)
          errp("seek failed");
        free(fname);
        break;
      }

      free(fname);
    }

    free(line);

    fputs(QTEGRITY_HASH_ALGO, fp_qtegrity_custom);
    fputs(":", fp_qtegrity_custom);
    fputs(file_digest, fp_qtegrity_custom);
    fputs(" file:", fp_qtegrity_custom);
    fputs(state.add_file, fp_qtegrity_custom);
    fputs("\n", fp_qtegrity_custom);

    flush_status = fflush(fp_qtegrity_custom);
    if (flush_status != 0)
      puts("Error flushing stream!");

    free(file_digest);
    fclose(fp_qtegrity_custom);
  }

  if (state.add)
    free(state.add_file);

  return EXIT_SUCCESS;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
