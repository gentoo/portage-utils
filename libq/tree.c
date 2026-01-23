/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <xalloc.h>

#if defined(ENABLE_GPKG) || defined(ENABLE_GTREE)
# include <archive.h>
# include <archive_entry.h>
#endif

#include "atom.h"
#include "eat_file.h"
#include "hash.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xpak.h"

/* 2026 rewrite
 * After releases 0.98 and 0.99 it became clear tree had become too much
 * loaded with functionality that broke things in many ways.  In
 * particular inconsistent behaviour, as well as crashes and leaks due
 * to seemingly random return of pointers or copies to be freed or not
 * based on the calling scenario.
 *
 * With the amount of tree types and the cost of traversals, combined
 * with the need to do selective versus full scans, caching what we read
 * is an integral part.  Each tree consists of a list of categories, and
 * per category a list of packages.
 * - categories: list, populated on first access
 *   * full scan flags the list as final, e.g. next requests can be
 *     handled via the cache (including negatives)
 *   * cat lookup adds the category to the list as existing
 *   * sorting is performed as necessary (on-demand)
 * - packages: list attached to their category, can be per package (PN)
 *   such as in ebuild trees, or full list for a category
 *   * stores name so cheap pre-filter can be done without atom_compare
 *   * starts shallow, further getters may retrieve more data
 *   * metadata may be filled in on demand, or while reading if the data
 *     is available anywya
 *   * sorting is performed as necessary like for categories
 *
 * Functionality from free is all shielded behind functions, code
 * outside of tree should not be able to poke in the internal state
 * structs, nor have the need for that.  Functions return pointers that
 * should not be freed, everything remains cached until the tree is
 * closed.  Exception to this is the matching interface which returns an
 * array that must be freed by the caller.  Callers should not modify
 * what they get returned, if they have to, they have to clone elements,
 * e.g. using atom_clone().
 *
 * Each tree keeps portroot filedescriptor open for as long as it is
 * around.  Other descriptors are only kept open for as long as the
 * operation to read or consume the data takes, in order to reduce the
 * potentially open descriptors.  The path elements of pkg and tree
 * structures point to the object relative to the portroot_fd and do not
 * include the leading '/' for that reason.
 *
 * The main two ways of interacting with trees (constructed via
 * tree_new) are:
 * - tree_foreach_pkg: iterate over all packages that match an atom via
 *   a callback function
 * - tree_match_atom: return all matches for an atom in an array
 * The two ways are essentially the same, tree_match_atom is implemented
 * by calling tree_foreach_pkg with a callback of its own to construct
 * the return array.
 */

struct tree_ {
  char          *path;
  char          *repo;
  array         *cats;         /* list of tree_cat_ctx pointers */
  int            portroot_fd;
  enum {
    TREE_UNSET = 0,
    TREE_EBUILD,
    TREE_VDB,
    TREE_PACKAGES,
    TREE_BINPKGS,
    TREE_GTREE,
  }              type;
  bool           cats_complete:1;
};

struct tree_cat_ {
  tree_ctx      *tree;
  char          *name;
  array         *pkgs;         /* list of tree_pkg_ctx pointers */
  bool           pkgs_complete:1;
};

struct tree_pkg_ {
  tree_cat_ctx  *cat;
  char          *name;         /* PN, for cheap search purposes */
  char          *path;         /* full path from root, includes extension etc */
  atom_ctx      *atom;         /* lazily initialised, thus initially NULL */
  char          *meta[TREE_META_MAX_KEYS];
  bool           atom_complete:1;
  bool           meta_complete:1;
  bool           cache_invalid:1;
  bool           binpkg_gpkg:1;
};

#ifdef ENABLE_GTREE
static tree_ctx *tree_new_gtree
(
  tree_ctx *ctx,
  bool      quiet
)
{
  struct archive       *gt;
  struct archive_entry *entry;
  int                   fd;

  fd = openat(ctx->portroot_fd, ctx->path, O_RDONLY | O_CLOEXEC);
  if (fd == -1)
  {
    if (!quiet)
      warnp("could not open gtree '/%s'", ctx->path);
    tree_close(ctx);
    return NULL;
  }

  gt = archive_read_new();
  archive_read_support_format_all(gt);
  if (archive_read_open_fd(gt, fd, BUFSIZ) != ARCHIVE_OK ||
      archive_read_next_header(gt, &entry) != ARCHIVE_OK)
  {
    if (!quiet)
      warn("could not open gtree '/%s': %s",
           ctx->path, archive_error_string(gt));
    archive_read_free(gt);
    close(fd);
    tree_close(ctx);
    return NULL;
  }

  if (strcmp(archive_entry_pathname(entry), "gtree-1") != 0)
  {
    if (!quiet)
      warn("could not open gtree '/%s': not a gtree container", ctx->path);
    archive_read_free(gt);
    close(fd);
    tree_close(ctx);
    return NULL;
  }

  ctx->type = TREE_GTREE;

  /* defer repo until the first read */
  archive_read_free(gt);
  close(fd);

  return ctx;
}

struct tree_gtree_cb_ctx {
  struct archive *archive;
};

static la_ssize_t tree_gtree_read_cb
(
  struct archive *a,
  void           *cctx,
  const void    **buf
)
{
  struct tree_gtree_cb_ctx *ctx    = cctx;
  size_t                    size;
  la_int64_t                offset;  /* unused */
  int                       ret;

  (void)a;

  ret = archive_read_data_block(ctx->archive, buf, &size, &offset);
  if (ret == ARCHIVE_EOF)
    return 0;
  if (ret != ARCHIVE_OK)
    return -1;
  (void)offset;
  /* at this point I sincerely hope size is not going to be over the
   * unsigned variant */
  return (la_ssize_t)size;
}

static int tree_gtree_close_cb
(
  struct archive *a,
  void           *cctx
)
{
  (void)a;
  (void)cctx;

  /* noop */
  return ARCHIVE_OK;
}

static int tree_foreach_pkg_gtree
(
  tree_ctx       *tree
)
{
  char                      buf[_Q_PATH_MAX];
  tree_cat_ctx             *cat         = NULL;
  tree_pkg_ctx             *pkg         = NULL;
  atom_ctx                 *atom        = NULL;
  struct archive           *outer;
  struct archive           *inner;
  struct archive_entry     *entry;
  char                     *p;
  char                     *rbuf        = NULL;
  struct tree_gtree_cb_ctx  cb_ctx;
  bool                      foundcaches = false;
  size_t                    len;
  size_t                    rlen        = 0;
  int                       ret         = 0;
  int                       fd;

  fd = openat(tree->portroot_fd, tree->path, O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 1;

  outer = archive_read_new();
  archive_read_support_format_all(outer);  /* don't see why not */
  if (archive_read_open_fd(outer, fd, BUFSIZ) != ARCHIVE_OK)
  {
    warn("unable to read gtree container: %s",
         archive_error_string(outer));
    archive_read_free(outer);
    return 1;
  }

  while (archive_read_next_header(outer, &entry) == ARCHIVE_OK)
  {
    const char *fname = archive_entry_pathname(entry);
    if (fname == NULL)
      continue;
    if (strncmp(fname, "repo.tar", sizeof("repo.tar") - 1) == 0 &&
        (fname[sizeof("repo.tar") - 1] == '.' ||
         fname[sizeof("repo.tar") - 1] == '\0'))
      break;
    entry = NULL;
  }
  if (entry == NULL) {
    archive_read_free(outer);
    return 1;
  }

  /* must be empty, we always read all cats */
  tree->cats = array_new();

  /* use wrapper to read straight from this archive */
  inner = archive_read_new();
  archive_read_support_format_all(inner);
  archive_read_support_filter_all(inner);
  VAL_CLEAR(cb_ctx);
  cb_ctx.archive = outer;
  if (archive_read_open(inner, &cb_ctx, NULL,
                        tree_gtree_read_cb,
                        tree_gtree_close_cb) != ARCHIVE_OK)
    warn("unable to read gtree data %s: %s", archive_entry_pathname(entry),
         archive_error_string(inner));

  while (archive_read_next_header(inner, &entry) == ARCHIVE_OK)
  {
    const char *fname = archive_entry_pathname(entry);

    if (fname == NULL)
      continue;

    if (tree->repo == NULL &&
        strcmp(fname, "repository") == 0)
    {
      /* fill in repo, so it can be used when requested */
      len = archive_entry_size(entry);
      tree->repo = xmalloc(len + 1);
      archive_read_data(inner, tree->repo, len);
      tree->repo[len] = '\0';
    }
    else if (strncmp(fname, "caches/", sizeof("caches/") - 1) == 0)
    {
      char *nexttok = NULL;

      foundcaches = true;
      fname += sizeof("caches/") - 1;
      atom = atom_explode(fname);

      if (cat == NULL ||
          strcmp(cat->name, atom->CATEGORY) != 0)
      {
        cat = xzalloc(sizeof(*cat));
        cat->name = xstrdup(atom->CATEGORY);
        cat->tree = tree;
        cat->pkgs = array_new();

        array_append(tree->cats, cat);
        /* not yet, but will be */
        cat->pkgs_complete = true;
      }

      /* we point to the ebuild, so it looks like md5-cache */
      snprintf(buf, sizeof(buf), "%s/ebuilds/%s/%s/%s.ebuild",
               tree->path, atom->CATEGORY, atom->PN, atom->PF);

      pkg = xzalloc(sizeof(*pkg));
      pkg->name = xstrdup(atom->PN);
      pkg->path = xstrdup(buf);
      pkg->cat  = cat;
      array_append(cat->pkgs, pkg);

      /* ok, we're in business */
      len = archive_entry_size(entry);
      if (len > rlen)
      {
        rlen = len + 1;
        rbuf = xrealloc(rbuf, rlen);
      }
      rbuf[len] = '\0';
      archive_read_data(inner, rbuf, len);

      /* entries are strictly single line, starting with KEY= (no
       * whitespace) */
      for (p = strtok_r(rbuf, "=", &nexttok);
           p != NULL;
           p = strtok_r(NULL, "=", &nexttok))
      {
        char **ptr = NULL;

        if (1 == 0) {
          /* dummy case for syntax */
        }
#define match(K) \
        else if (strcmp(p, #K) == 0) \
          do { \
            ptr = &pkg->meta[Q_##K]; \
          } while (0)
        match(DEPEND);
        match(RDEPEND);
        match(SLOT);
        match(SRC_URI);
        match(RESTRICT);
        match(LICENSE);
        match(DESCRIPTION);
        match(KEYWORDS);
        match(INHERITED);
        match(IUSE);
        match(CDEPEND);
        match(PDEPEND);
        match(PROVIDE);
        match(EAPI);
        match(PROPERTIES);
        match(BDEPEND);
        match(IDEPEND);
        match(DEFINED_PHASES);
        match(REQUIRED_USE);
        match(CONTENTS);
        match(USE);
        match(EPREFIX);
        match(PATH);
        match(BUILD_ID);
        match(SIZE);
        match(_eclasses_);
#undef match

        /* always advance to end of line, even when nothing
         * matched */
        p = strtok_r(NULL, "\n", &nexttok);
        if (p == NULL)
          break;
        if (ptr != NULL &&
            *ptr == NULL)
          *ptr = xstrdup(p);
      }

      pkg->meta_complete = true;
    } else if (foundcaches) {
      break;  /* stop searching if we processed all cache entries */
    }
  }

  free(rbuf);

  tree->cats_complete = true;

  return ret;
}
#endif

/* opens the tree at path inside portroot and returns a tree object
 * ready for traversing packages or NULL if an error occurred for which
 * the reason would be printed to stderr unless quiet is set */
tree_ctx *tree_new
(
  const char         *portroot,
  const char         *path,
  enum tree_open_type type,
  bool                quiet
)
{
  tree_ctx    *ret;
  struct stat  st;

  if (portroot == NULL ||
      path == NULL ||
      portroot[0] != '/')
    return NULL;

  /* we require absolute portroot, but path may not start with / in
   * which case we assume it to be relative to portroot */
  if (path[0] == '/')
  {
    if (path[1] == '\0')
      path = ".";
    else
      path++;
  }

  ret = xzalloc(sizeof(*ret));
  ret->portroot_fd = open(portroot, O_RDONLY | O_PATH);
  if (ret->portroot_fd == -1) {
    if (!quiet)
      warnp("could not open root '%s'", portroot);
    tree_close(ret);
    return NULL;
  }

  VAL_CLEAR(st);
  if (fstatat(ret->portroot_fd, path, &st, 0) < 0)
  {
    if (!quiet)
      warnp("could not open tree '/%s'", path);
    tree_close(ret);
    return NULL;
  }
  ret->path = xstrdup(path);

  switch (type)
  {
  case TREETYPE_EBUILD: /* {{{ */
    {
      char   buf[_Q_PATH_MAX];
      size_t len;

      if (!S_ISDIR(st.st_mode))
      {
        if (!quiet)
          warn("invalid path '/%s' for ebuild tree: must be a directory", path);
        tree_close(ret);
        return NULL;
      }

#ifdef ENABLE_GTREE
      /* look for gtree, in which case we will ignore this tree in
       * favour of the gtree container */
      snprintf(buf, sizeof(buf), "%s/metadata/repo.gtree.tar", path);
      VAL_CLEAR(st);
      if (fstatat(ret->portroot_fd, buf, &st, 0) == 0 &&
          S_ISREG(st.st_mode))
      {
        free(ret->path);
        ret->path = xstrdup(buf);
        return tree_new_gtree(ret, quiet);
      }
#endif

      ret->type = TREE_EBUILD;

      snprintf(buf, sizeof(buf), "%s/profiles/repo_name", path);
      if (eat_file_at(ret->portroot_fd, buf, &ret->repo, &len) != 0)
        (void)rmspace(ret->repo);
    }
    break; /* }}} */
  case TREETYPE_VDB: /* {{{ */
    if (!S_ISDIR(st.st_mode))
    {
      if (!quiet)
        warn("invalid path '/%s' for VDB tree: must be a directory", path);
      tree_close(ret);
      return NULL;
    }

    ret->type = TREE_VDB;
    break; /* }}} */
  case TREETYPE_BINPKG: /* {{{ */
    {
      char   buf[_Q_PATH_MAX];

      if (!S_ISDIR(st.st_mode))
      {
        if (!quiet)
          warn("invalid path '/%s' for binpkg tree: must be a directory", path);
        tree_close(ret);
        return NULL;
      }

      ret->type = TREE_BINPKGS;

      snprintf(buf, sizeof(buf), "%s/Packages", path);
      if (fstatat(ret->portroot_fd, buf, &st, 0) == 0 &&
          S_ISREG(st.st_mode))
      {
        free(ret->path);
        ret->path = xstrdup(buf);
        ret->type = TREE_PACKAGES;
      }

      /* TODO: we can read the Packages.gz file too, need to elevate
       * zlib check in configure, to unpack it */
    }
    break; /* }}} */
  case TREETYPE_GTREE: /* {{{ */
#ifdef ENABLE_GTREE
    ret = tree_new_gtree(ret, quiet);
#else
    return NULL;
#endif
    break; /* }}} */
  default:
    if (!quiet)
      warn("invalid tree type");
    tree_close(ret);
    return NULL;
  }

  return ret;
}

/* helper to free up resources held by a package */
static void tree_pkg_close
(
  tree_pkg_ctx *pkg
)
{
  enum tree_pkg_meta_keys k;

  if (pkg == NULL)
    return;

  if (pkg->atom != NULL)
    atom_implode(pkg->atom);
  for (k = Q_UNKNOWN; k < TREE_META_MAX_KEYS; k++)
    free(pkg->meta[k]);
  free(pkg->path);
  free(pkg->name);
  free(pkg);
}

/* helper to free up resources held by a category */
static void tree_cat_close
(
  tree_cat_ctx *cat
)
{
  if (cat == NULL)
    return;

  array_deepfree(cat->pkgs, (array_free_cb *)tree_pkg_close);

  free(cat->name);
  free(cat);
}

/* close and free up resources held by this tree context and its
 * subtrees, if any */
void tree_close
(
  tree_ctx *tree
)
{
  if (tree == NULL)
    return;

  array_deepfree(tree->cats, (array_free_cb *)tree_cat_close);

  free(tree->path);
  free(tree->repo);

  if (tree->portroot_fd >= 0)
    close(tree->portroot_fd);

  free(tree);
}

/* helper to read the contents of a VDB key for a package */
static bool tree_pkg_vdb_eat
(
  tree_pkg_ctx *pkg,
  const char   *file,
  char        **bufptr,
  size_t       *buflen
)
{
  char buf[_Q_PATH_MAX];
  int  fd;
  bool ret;

  snprintf(buf, sizeof(buf), "%s/%s", pkg->path, file);

  if ((fd = openat(pkg->cat->tree->portroot_fd, buf, O_RDONLY, 0)) < 0)
    return false;

  ret = eat_file_fd(fd, bufptr, buflen);
  if (ret)
    rmspace(*bufptr);

  close(fd);
  return ret;
}

/* read full md5-cache entry into pkgs' meta */
static bool tree_pkg_md5_read
(
  tree_pkg_ctx *pkg,
  char         *path
)
{
  char       *data;
  char       *nexttok = NULL;
  char       *p;
  size_t      len;
  int         fd;
  bool        ret;

  if ((fd = openat(pkg->cat->tree->portroot_fd, path, O_RDONLY, 0)) < 0)
    return false;

  data = NULL;
  len  = 0;
  ret  = eat_file_fd(fd, &data, &len);
  close(fd);

  if (!ret)
    return false;

  /* We have a block of key=value\n data.
   * KEY=VALUE\n
   * Where KEY does NOT contain:
   * \0 \t\n =
   * And VALUE does NOT contain:
   * \0\n
   * */

  for (p = strtok_r(data, "=", &nexttok);
       p != NULL;
       p = strtok_r(NULL, "=", &nexttok))
  {
    char **ptr = NULL;

    if (1 == 0) {
      /* dummy case for syntax */
    }
#define match(K) \
    else if (strcmp(p, #K) == 0) \
      ptr = &pkg->meta[Q_##K]
    match(DEPEND);
    match(RDEPEND);
    match(SLOT);
    match(SRC_URI);
    match(RESTRICT);
    match(LICENSE);
    match(DESCRIPTION);
    match(KEYWORDS);
    match(INHERITED);
    match(IUSE);
    match(CDEPEND);
    match(PDEPEND);
    match(PROVIDE);
    match(EAPI);
    match(PROPERTIES);
    match(BDEPEND);
    match(IDEPEND);
    match(DEFINED_PHASES);
    match(REQUIRED_USE);
    match(CONTENTS);
    match(USE);
    match(EPREFIX);
    match(PATH);
    match(BUILD_ID);
    match(SIZE);
    match(_eclasses_);
    match(_md5_);
#undef match

    /* always advance to end of line, even when nothing
     * matched */
    p = strtok_r(NULL, "\n", &nexttok);
    if (p == NULL)
      break;
    if (ptr != NULL &&
        *ptr == NULL)
      *ptr = xstrdup(p);
  }

  free(data);

  return true;
}

/* attempt to parse ebuild file into pkgs' meta */
static bool tree_pkg_ebuild_read
(
  tree_pkg_ctx *pkg
)
{
  char       *p;
  char       *q;
  char       *w;
  char      **key;
  size_t      len;
  int         fd;
  bool        esc;
  bool        findnl;
  bool        ret;

  if ((fd = openat(pkg->cat->tree->portroot_fd, pkg->path, O_RDONLY, 0)) < 0)
    return false;

  p   = NULL;
  len = 0;
  ret = eat_file_fd(fd, &p, &len);
  close(fd);

  if (!ret)
    return false;

  do
  {
    /* leading whitespace is allowed */
    while (isspace((int)*p))
      p++;
    q = p;
    while (*p >= 'A' &&
           *p <= 'Z')
      p++;

    key = NULL;
    if (q < p &&
        *p == '=')
    {
      *p++ = '\0';
      /* match variable against which ones we look for */
      if (1 == 0); /* dummy for syntax */
#define match_key(X) \
      else if (strcmp(q, #X) == 0) key = &pkg->meta[Q_##X]
      match_key(DEPEND);
      match_key(RDEPEND);
      match_key(SLOT);
      match_key(SRC_URI);
      match_key(RESTRICT);
      match_key(HOMEPAGE);
      match_key(LICENSE);
      match_key(DESCRIPTION);
      match_key(KEYWORDS);
      match_key(IUSE);
      match_key(CDEPEND);
      match_key(PDEPEND);
      match_key(EAPI);
      match_key(REQUIRED_USE);
      match_key(BDEPEND);
      match_key(IDEPEND);
#undef match_key
    }

    findnl = true;
    if (key != NULL)
    {
      q = p;
      if (*q == '"' ||
          *q == '\'')
      {
        /* find matching quote */
        p++;
        w = p;
        esc = false;
        do
        {
          while (*p != '\0' &&
                 *p != *q)
          {
            if (*p == '\\')
            {
              esc = !esc;
              if (esc)
              {
                p++;
                continue;
              }
            }
            else
            {
              /* stash everything on a single line like
               * VDB and md5-cache do */
              if (*p == '\n' ||
                  *p == '\r')
                *p = ' ';
              esc = false;
            }

            /* collapse sequences of spaces */
            if (*w != ' ' ||
                *p != ' ')
              *w++ = *p++;
            else
              p++;
          }
          if (*p == *q &&
              esc)
          {
            /* escaped, move along */
            esc  = false;
            *w++ = *p++;
            continue;
          }
          break;
        }
        while (true);
        q++;
        *w = '\0';
      }
      else
      {
        /* find first whitespace */
        while (!isspace((int)*p))
          p++;
        if (*p == '\n')
          findnl = false;
      }
      *p++ = '\0';
      if (*key == NULL)  /* ignore secondary assignments (perhaps if/else) */
        *key = xstrdup(q);
    }

    if (findnl &&
        (p = strchr(p, '\n')) != NULL)
      p++;
  }
  while (*p != '\0');

  return true;
}

static void tree_pkg_xpak_read_cb
(
    void *ctx,
    char *pathname,
    int   pathname_len,
    int   data_offset,
    int   data_len,
    char *data)
{
  tree_pkg_ctx  *pkg = ctx;
  char         **key;

#define match_path(K) \
  else if (pathname_len == (sizeof(#K) - 1) && \
           strcmp(pathname, #K) == 0) \
    do { \
      key = &pkg->meta[Q_##K]; \
    } while (false)

  if (1 == 0); /* dummy for syntax */
  match_path(DEPEND);
  match_path(RDEPEND);
  match_path(SLOT);
  match_path(SRC_URI);
  match_path(RESTRICT);
  match_path(HOMEPAGE);
  match_path(DESCRIPTION);
  match_path(KEYWORDS);
  match_path(INHERITED);
  match_path(IUSE);
  match_path(CDEPEND);
  match_path(PDEPEND);
  match_path(PROVIDE);
  match_path(EAPI);
  match_path(PROPERTIES);
  match_path(DEFINED_PHASES);
  match_path(REQUIRED_USE);
  match_path(BDEPEND);
  match_path(IDEPEND);
  match_path(CONTENTS);
  match_path(USE);
  match_path(EPREFIX);
  match_path(repository);
  else
    return;
#undef match_path

  /* don't overwrite entries */
  if (*key != NULL)
    return;

  /* trim whitespace (mostly trailing newline) */
  while (data_len > 0 &&
         isspace((int)data[data_offset + data_len - 1]))
    data_len--;

  /* copy the entry into the meta */
  *key = xmemdup(data + data_offset, data_len + 1);
  (*key)[data_len] = '\0';
}

static bool tree_pkg_binpkg_read
(
  tree_pkg_ctx *pkg
)
{
  int fd;

  if (pkg->binpkg_gpkg)
  {
#ifdef ENABLE_GPKG
    struct archive       *a     = archive_read_new();
    struct archive_entry *entry;
    size_t                len   = 0;
    char                 *buf   = NULL;

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    fd = openat(pkg->cat->tree->portroot_fd, pkg->cat->tree->path, O_RDONLY);
    if (fd < 0)
      return false;

    if (archive_read_open_fd(a, fd, BUFSIZ) != ARCHIVE_OK)
    {
      close(fd);
      return false;
    }
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
      const char *pathname = archive_entry_pathname(entry);
      const char *fname    = strchr(pathname, '/');
      if (fname == NULL)
        continue;
      fname++;
      if (strncmp(fname, "metadata.tar",
                  sizeof("metadata.tar") - 1) == 0)
      {
        /* read this nested tar, it contains the VDB entries
         * otherwise stored in xpak */
        len = archive_entry_size(entry);
        buf = xmalloc(len);
        archive_read_data(a, buf, len);
        break;
      }
    }
    archive_read_free(a);
    close(fd);

    if (buf != NULL)
    {
      char  *data      = NULL;
      size_t data_size = 0;
      size_t data_len  = 0;

      a = archive_read_new();
      archive_read_support_format_all(a);
      archive_read_support_filter_all(a);
      archive_read_open_memory(a, buf, len);

      while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        char       *fname    = strchr(pathname, '/');
        if (fname == NULL)
          continue;
        fname++;

        data_len = archive_entry_size(entry);
        if (data_len > data_size) {
          data_size = data_len;
          data      = xrealloc(data, data_size);
        }
        if (archive_read_data(a, data, data_len) < 0)
          continue;
        tree_pkg_xpak_read_cb(pkg, fname, (int)strlen(fname),
                              0, data_len, data);
      }
      archive_read_free(a);
      free(buf);
      free(data);
    }
#else
    return false;
#endif
  }
  else
  {
    fd = openat(pkg->cat->tree->portroot_fd, pkg->path, O_RDONLY);
    if (fd < 0)
      return false;
    xpak_process_fd(fd, true, pkg, tree_pkg_xpak_read_cb);
    /* xpak_process closes the input fd */
  }

  /* get some numbers to emulate Packages for convenience */
  if (pkg->meta[Q_SIZE] == NULL ||
      pkg->meta[Q_MD5] == NULL ||
      pkg->meta[Q_SHA1] == NULL)
  {
    char   md5[MD5_DIGEST_LENGTH + 1];
    char   sha1[SHA1_DIGEST_LENGTH + 1];
    size_t flen;

    fd = openat(pkg->cat->tree->portroot_fd, pkg->path, O_RDONLY);
    if (fd < 0)
      return false;

    if (hash_multiple_file_fd(fd, md5, sha1, NULL, NULL,
                              NULL, &flen, HASH_MD5 | HASH_SHA1) == 0)
    {
      if (pkg->meta[Q_MD5] == NULL)
        pkg->meta[Q_MD5] = xstrdup(md5);
      if (pkg->meta[Q_SHA1] == NULL)
        pkg->meta[Q_SHA1] = xstrdup(sha1);

      if (pkg->meta[Q_SIZE] == NULL)
      {
        snprintf(md5, sizeof(md5), "%zu", flen);
        pkg->meta[Q_SIZE] = xstrdup(md5);
      }
    }
    /* fd is closed by hash_multiple_file_fd */
  }
  
  return true;
}

static const char *tree_meta_key_name[] = {
  "unknown",
  TREE_META_KEYS(TREE_META_KEY_NAME)
};

/* returns the value for the given metadata key, or NULL if absent
 * all values returned are strings, the caller should copy the strings
 * before modifying them */
char *tree_pkg_meta
(
  tree_pkg_ctx           *pkg,
  enum tree_pkg_meta_keys key
)
{
  if (key == Q_UNKNOWN ||
      key >= TREE_META_MAX_KEYS)
    return NULL;

  /* repository/REPO is an odd one out, it doesn't live in the pkg meta,
   * but in the tree instead for some repo types */
  if (key == Q_repository &&
      pkg->cat->tree->repo != NULL)
    return pkg->cat->tree->repo;
  /* similar for PATH which is only in Packages file */
  if (key == Q_PATH)
    return pkg->path;

  if (pkg->meta[key] == NULL &&
      !pkg->meta_complete)
  {
    tree_ctx *tree = pkg->cat->tree;
    
    switch (tree->type)
    {
    case TREE_EBUILD: /* {{{ */
      if (!pkg->cache_invalid)
      {
        char      buf[_Q_PATH_MAX];
        atom_ctx *atom = tree_pkg_atom(pkg, false);

        /* attempt to read the cache file from the location it should be */
        snprintf(buf, sizeof(buf), "%s/metadata/md5-cache/%s/%s",
                 tree->path, atom->CATEGORY, atom->PF);
        if (tree_pkg_md5_read(pkg, buf))
        {
          char   *mdmd5;
          char    srcmd5[MD5_DIGEST_LENGTH + 1];
          size_t  flen;
          int     k;

          /* in this case a cache entry exists, however, it may be
           * out of date, for that we need to check the md5 hashes
           * with the ebuild/eclass files, obviously when the source
           * ebuild doesn't exist, we never get here */

          if (hash_multiple_file_at(tree->portroot_fd, pkg->path,
                                    srcmd5, NULL, NULL, NULL,
                                    NULL, &flen, HASH_MD5) == 0)
          {
            mdmd5 = pkg->meta[Q__md5_];

            /* TODO: eclass compares */

            /* is this a valid cache? use it! */
            if (mdmd5 != NULL &&
                memcmp(mdmd5, srcmd5, MD5_DIGEST_LENGTH) == 0)
            {
              pkg->meta_complete = true;
              break;
            }
          }

          /* we read the meta, but apparently it was wrong, so clear
           * whatever we read */
          for (k = Q_UNKNOWN; k < TREE_META_MAX_KEYS; k++)
            free(pkg->meta[k]);
        }

        /* avoid trying to do this again */
        pkg->cache_invalid = true;
      }
        
      /* if we reach down here, we'll have to try and get what we need
       * from the ebuild itself */
      if (tree_pkg_ebuild_read(pkg))
        pkg->meta_complete = true;
      /* else, the file is not readable or doesn't exist, which is
       * funny, so we'll take the hit for it every time */

      break; /* }}} */
    case TREE_VDB: /* {{{ */
      {
        size_t len;
        tree_pkg_vdb_eat(pkg, tree_meta_key_name[key], &pkg->meta[key], &len);
      }
      break; /* }}} */
    case TREE_BINPKGS:
    case TREE_PACKAGES:
      {
        if (tree_pkg_binpkg_read(pkg))
          pkg->meta_complete = true;
      }
      break;
    default:
      break;
    }
  }

  return pkg->meta[key];
}

/* returns the atom for the package, performing possibly more expensive
 * measures to find SLOT and REPO when full is set to true */
atom_ctx *tree_pkg_atom
(
  tree_pkg_ctx *pkg,
  bool          full
)
{
  if (pkg->atom != NULL)
  {
    if (pkg->atom_complete ||
        !full)
      return pkg->atom;
  }

  if (pkg->atom == NULL)
  {
    char *p;

    /* while atom_explode is fine with getting a full path, it does
     * pre-calculate necessary storage size based on the input, so while
     * maybe confusing, it also wastes a lot of memory, which we want to
     * avoid here, so search for the last path component */
    p = strrchr(pkg->path, '/');
    if (p == NULL)
      p = pkg->path;
    else
      p++;

    pkg->atom = atom_explode_cat(p, pkg->cat->name);
  }

  if (full)
  {
    if (pkg->atom->REPO == NULL)
      pkg->atom->REPO = tree_pkg_meta(pkg, Q_repository);
    if (pkg->atom->SLOT == NULL)
    {
      pkg->atom->SLOT = tree_pkg_meta(pkg, Q_SLOT);
      if (pkg->atom->SLOT != NULL)
      {
        char *p;

        /* this is a bit atom territory, but since we pulled in SLOT we
         * need to split it up in SLOT and SUBSLOT for atom_format to
         * behave properly, this may be redundant but this probably
         * isn't much of an issue performance wise (on top of doing it
         * in atom_explode when given as input) */
        if ((p = strchr(pkg->atom->SLOT, '/')) != NULL)
        {
          *p++ = '\0';
        }
        else
        {
          /* PMS 7.2: When the sub-slot part is omitted from the
           * SLOT definition, the package is considered to have an
           * implicit sub-slot which is equal to the regular slot. */
          p = pkg->atom->SLOT;
        }
        pkg->atom->SUBSLOT = p;
      }
    }

    pkg->atom_complete = true;
  }

  return pkg->atom;
}

/* comparator function for category cache, operating on a tree_cat_ctx */
static int tree_cat_compar
(
  const void *l,
  const void *r
)
{
  tree_cat_ctx *left  = *(tree_cat_ctx **)l;
  tree_cat_ctx *right = *(tree_cat_ctx **)r;

  if (left == NULL &&
      right == NULL)
    return 0;
  else if (left == NULL)
    return 1;
  else if (right == NULL)
    return -1;
  else
    return strcmp(left->name, right->name);
}

/* comparator function for packages cache, operating on a tree_pkg_ctx
 * the comparison is "cheap" using strcmp only, when either side does
 * not have an atom populated, otherwise atom_compare is used to order
 * the results with identical name (PN)
 * NOTE: this comparator does not respect atom CATEGORY, as it assumes
 *       it is only sorting packages inside a category */
static int tree_pkg_compar
(
  const void *d,
  const void *q
)
{
  tree_pkg_ctx *data  = *(tree_pkg_ctx **)d;
  tree_pkg_ctx *query = *(tree_pkg_ctx **)q;

  if (data == NULL &&
      query == NULL)
    return 0;
  else if (data == NULL)
    return 1;
  else if (query == NULL)
    return -1;
  else
  {
    int ret = strcmp(data->name, query->name);
    if (ret == 0)
    {
      atom_ctx *qa = tree_pkg_atom(query, false);
      atom_ctx *da;

      /* ensure we fetch SLOT and REPO when the query includes them, so
       * we have a chance of matching */
      da = tree_pkg_atom(data, (qa->SLOT != NULL ||
                                qa->REPO != NULL));

      /* never makes sense to compare repository here */
      switch (atom_compare(da, qa))
      {
      case EQUAL:  ret =  0;  break;
      case NEWER:  ret = -1;  break;
      case OLDER:  ret =  1;  break;
      case NOT_EQUAL:
      default:
        /* we only get here when PN is equal, so NOT_EQUAL is impossible
         * unless there's operators used on query, which should only be
         * when we're matching results */
                   ret = -1;  break;
      }
    }
    return ret;
  }
}

/* helper to delect valid category names from readdir */
static int tree_filter_cat
(
  const struct dirent *de
)
{
  int  i;
  bool founddash;

  /* PMS 3.1.1 */
  founddash = false;
  for (i = 0; de->d_name[i] != '\0'; i++)
  {
    switch (de->d_name[i])
    {
    case '_':
      break;
    case '-':
      founddash = true;
      /* fall through */
    case '+':
    case '.':
      if (i)
        break;
      return 0;
    default:
      if ((de->d_name[i] >= 'A' &&
           de->d_name[i] <= 'Z') ||
          (de->d_name[i] >= 'a' &&
           de->d_name[i] <= 'z') ||
          (de->d_name[i] >= '0' &&
           de->d_name[i] <= '9'))
        break;
      return 0;
    }
  }
  if (!founddash &&
      strcmp(de->d_name, "virtual") != 0)
    return 0;

  return i;
}

/* helper to delect valid package names from readdir */
static int tree_filter_pkg
(
  const struct dirent *de
)
{
  int  i;
  bool founddash = false;

  /* PMS 3.1.2 */
  for (i = 0; de->d_name[i] != '\0'; i++)
  {
    switch (de->d_name[i])
    {
    case '_':
      break;
    case '-':
      founddash = true;
      /* fall through */
    case '+':
      if (i)
        break;
      return 0;
    default:
      if ((de->d_name[i] >= 'A' &&
           de->d_name[i] <= 'Z') ||
          (de->d_name[i] >= 'a' &&
           de->d_name[i] <= 'z') ||
          (de->d_name[i] >= '0' &&
           de->d_name[i] <= '9'))
        break;
      if (founddash)
        return 1;
      return 0;
    }
  }

  if (i > 0 &&
      (strcmp(de->d_name, "Manifest") == 0 ||
       strcmp(de->d_name, "metadata.xml") == 0))
    i = 0;

  return i;
}

/* iterates over the given category in its tree, invoking the callback
 * function for packages matching the query */
static int tree_cat_foreach_pkg
(
  tree_cat_ctx   *cat,
  tree_pkg_cb     callback,
  void           *priv,
  bool            sorted,
  const atom_ctx *query
)
{
  tree_ctx       *tree     = cat->tree;
  tree_pkg_ctx   *pkg;
  tree_pkg_ctx   *nref;
  tree_pkg_ctx    needle;
  char           *pn       = NULL;
  int             ret      = 0;
  bool            filterpn = false;

  VAL_CLEAR(needle);
  nref = &needle;

  if (query != NULL &&
      query->PN != NULL)
    filterpn = true;
  
  /* handle directed query on our cache */
  if (filterpn &&
      cat->pkgs != NULL)
  {
    size_t       elem;

    needle.name = query->PN;
    needle.path = query->PN;
    needle.cat  = cat;
    /* keep atom away while we search for the package, we want to know
     * the package is seen (in the cache), so we can also handle
     * negative responses here */
    pkg = array_binsearch(cat->pkgs, &needle, tree_pkg_compar, &elem);
    /* cleanup atom created implicitly for comparison */
    if (needle.atom)
      atom_implode(needle.atom);
    if (pkg != NULL)
    {
      /* now use the original atom to refine the query */
      needle.atom = (atom_ctx *)query;

      do
      {
        if (tree_pkg_compar(&pkg, &nref) == 0)
          ret |= callback(pkg, priv);
      }
      while (++elem < array_cnt(cat->pkgs) &&
             (pkg = array_get(cat->pkgs, elem)) != NULL &&
             strcmp(nref->name, pkg->name) == 0);

      return ret;
    }
    else if (cat->pkgs_complete)
    {
      return 0;
    }

    VAL_CLEAR(needle);
  }

  /* for below to run, there must be no filter */
  if (cat->pkgs_complete)
  {
    size_t n;

    if (sorted)
      array_sort(cat->pkgs, tree_pkg_compar);

    array_for_each(cat->pkgs, n, pkg)
      ret |= callback(pkg, priv);

    return ret;
  }

  if (filterpn)
  {
    needle.name = query->PN;
    needle.atom = (atom_ctx *)query;
  }

  switch (tree->type)
  {
  case TREE_EBUILD: /* {{{ */
    {
      char           buf[_Q_PATH_MAX];
      tree_pkg_ctx   lookup;
      struct stat    sb;
      struct dirent *de;
      DIR           *catdir   = NULL;
      int            catfd    = -1;
      bool           domatch  = cat->pkgs != NULL;
      bool           mfound   = false;

      VAL_CLEAR(lookup);

      /* if query has PN, then attempt opening it and load the ebuilds
       * as pkgs into the cache, else do full iteration over dirs and
       * all ebuilds inside */
      if (filterpn)
      {
        pn = query->PN;
      }
      else
      {
        snprintf(buf, sizeof(buf), "%s/%s", tree->path, cat->name);
        if ((catfd = openat(tree->portroot_fd, buf, O_RDONLY | O_CLOEXEC)) < 0)
          return 0;
        if ((catdir = fdopendir(catfd)) == NULL)
        {
          close(catfd);
          return 0;
        }
      }

      for (;; pn = NULL)
      {
        DIR        *dir;
        size_t      len;
        int         fd;

        if (pn == NULL)
        {
          if (catfd == -1)
            break;

          /* load next PN dir */
          if ((de = readdir(catdir)) == NULL)
            break;

          if (tree_filter_pkg(de) == 0)
            continue;

          pn = de->d_name;

          if (domatch)
          {
            lookup.name = pn;
            lookup.path = pn;
            lookup.atom = NULL;
            pkg = array_binsearch(cat->pkgs, &lookup, tree_pkg_compar, NULL);
            if (lookup.atom != NULL)
              atom_implode(lookup.atom);
            if (pkg != NULL)
              continue;
          }
        }

        len = snprintf(buf, sizeof(buf), "%s/%s/%s",
                       tree->path, cat->name, pn);
        if ((fd = openat(tree->portroot_fd, buf, O_RDONLY | O_CLOEXEC)) < 0)
          continue;
        if ((dir = fdopendir(fd)) == NULL)
        {
          close(fd);
          continue;
        }

        while (true)
        {
          size_t         nlen;

          if ((de = readdir(dir)) == NULL)
            break;

          nlen = strlen(de->d_name);
          if (nlen <= sizeof(".ebuild") - 1 ||
              memcmp(de->d_name + (nlen - (sizeof(".ebuild") - 1)),
                     ".ebuild", (sizeof(".ebuild") - 1)) != 0)
            continue;

          snprintf(buf + len, sizeof(buf) - len, "/%.*s",
                   (int)nlen, de->d_name);
          if (fstatat(tree->portroot_fd, buf, &sb, 0) < 0 ||
              !S_ISREG(sb.st_mode))
            continue;

          pkg       = xzalloc(sizeof(*pkg));
          pkg->name = xstrdup(pn);
          pkg->path = xstrdup(buf);
          pkg->cat  = cat;

          if (cat->pkgs == NULL)
            cat->pkgs = array_new();
          array_append(cat->pkgs, pkg);
          mfound = true;

          if (!sorted &&
              (!filterpn ||
               tree_pkg_compar(&pkg, &nref) == 0))
            ret |= callback(pkg, priv);
        }

        closedir(dir);
      }

      if (catfd != -1)
        closedir(catdir);

      if (!filterpn)
        cat->pkgs_complete = true;

      if (mfound &&
          sorted)
      {
        /* recurse, now use the built cache */
        ret = tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
      }

      return ret;
    }
    break; /* }}} */
  case TREE_BINPKGS: /* {{{ */
    {
      char           buf[_Q_PATH_MAX * 2];
      struct stat    sb;
      struct dirent *de;
      DIR           *catdir = NULL;
      int            catfd  = -1;
      size_t         len;

      /* this is hybrid these days with PN dirs and files, possibly being
       * for the same PN too ... so we'll be just listing the whole lot
       * and recurse through directories as we see them, while we could
       * limit our search to PN directories, since we cannot be sure
       * there are no files for the same PN too, we just do the whole
       * thing and let the cache sort it out to ensure something
       * coherent */

      len = snprintf(buf, sizeof(buf), "%s/%s", tree->path, cat->name);
      if ((catfd = openat(tree->portroot_fd, buf, O_RDONLY | O_CLOEXEC)) < 0)
        return 0;
      if ((catdir = fdopendir(catfd)) == NULL)
      {
        close(catfd);
        return 0;
      }

      if (filterpn)
      {
        VAL_CLEAR(needle);
        needle.name = query->PN;
        needle.atom = (atom_ctx *)query;
      }

      if (cat->pkgs == NULL)
        cat->pkgs = array_new();

      while (true)
      {
        size_t pnlen;
        size_t nlen;
        int    fd;

        if ((de = readdir(catdir)) == NULL)
          break;

        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' &&
              de->d_name[2] == '\0')))
          continue;

        pnlen = snprintf(buf + len, sizeof(buf) - len, "/%s", de->d_name);
        if ((fd = openat(tree->portroot_fd, buf, O_RDONLY | O_CLOEXEC)) < 0 ||
            fstat(fd, &sb) < 0)
          continue;

        if (S_ISDIR(sb.st_mode))
        {
          DIR *dir;

          /* PN dir, need to look inside */
          if (tree_filter_pkg(de) == 0 ||
              (dir = fdopendir(fd)) == NULL)
          {
            close(fd);
            continue;
          }

          while (true)
          {
            if ((de = readdir(dir)) == NULL)
              break;

            snprintf(buf + len + pnlen, sizeof(buf) - len - pnlen,
                     "/%s", de->d_name);
            if (fstatat(tree->portroot_fd, buf, &sb, 0) < 0 ||
                !S_ISREG(sb.st_mode))
              continue;

            /* Portage says that when inside a PN (multi-binpkg) the
             * extension of pre-gpkg files is xpak, as opposed to .tbz2 */
            nlen = strlen(de->d_name);
            if (nlen > sizeof(".xpak") - 1 &&
                memcmp(de->d_name + (nlen - (sizeof(".xpak") - 1)),
                       ".xpak", (sizeof(".xpak") - 1)) == 0)
            {
              pkg       = xzalloc(sizeof(*pkg));
              pkg->atom = atom_explode_cat(de->d_name, cat->name);
              pkg->name = xstrdup(pkg->atom->PN);
              pkg->path = xstrdup(buf);
              pkg->cat  = cat;

              array_append(cat->pkgs, pkg);

              if (!sorted &&
                  (!filterpn ||
                   tree_pkg_compar(&pkg, &nref) == 0))
                ret |= callback(pkg, priv);
            }
            else if (nlen > sizeof(".gpkg.tar") - 1 &&
                     memcmp(de->d_name + (nlen - (sizeof(".gpkg.tar") - 1)),
                            ".gpkg.tar", (sizeof(".gpkg.tar") - 1)) == 0)
            {
              pkg              = xzalloc(sizeof(*pkg));
              pkg->atom        = atom_explode_cat(de->d_name, cat->name);
              pkg->name        = xstrdup(pkg->atom->PN);
              pkg->path        = xstrdup(buf);
              pkg->cat         = cat;
              pkg->binpkg_gpkg = true;

              array_append(cat->pkgs, pkg);

              if (!sorted &&
                  (!filterpn ||
                   tree_pkg_compar(&pkg, &nref) == 0))
                ret |= callback(pkg, priv);
            }
          }
          closedir(dir);
        }
        else
        {
          /* regular file */
          nlen = strlen(de->d_name);
          if (nlen > sizeof(".tbz2") - 1 &&
              memcmp(de->d_name + (nlen - (sizeof(".tbz2") - 1)),
                     ".tbz2", (sizeof(".tbz2") - 1)) == 0)
          {
            pkg       = xzalloc(sizeof(*pkg));
            pkg->atom = atom_explode_cat(de->d_name, cat->name);
            pkg->name = xstrdup(pkg->atom->PN);
            pkg->path = xstrdup(buf);
            pkg->cat  = cat;

            array_append(cat->pkgs, pkg);

            if (!sorted &&
                (!filterpn ||
                 tree_pkg_compar(&pkg, &nref) == 0))
              ret |= callback(pkg, priv);
          }
          else if (nlen > sizeof(".gpkg.tar") - 1 &&
                   memcmp(de->d_name + (nlen - (sizeof(".gpkg.tar") - 1)),
                          ".gpkg.tar", (sizeof(".gpkg.tar") - 1)) == 0)
          {
            pkg              = xzalloc(sizeof(*pkg));
            pkg->atom        = atom_explode_cat(de->d_name, cat->name);
            pkg->name        = xstrdup(pkg->atom->PN);
            pkg->path        = xstrdup(buf);
            pkg->cat         = cat;
            pkg->binpkg_gpkg = true;

            array_append(cat->pkgs, pkg);

            if (!sorted &&
                (!filterpn ||
                 tree_pkg_compar(&pkg, &nref) == 0))
              ret |= callback(pkg, priv);
          }

          close(fd);
        }
      }

      closedir(catdir);

      cat->pkgs_complete = true;

      if (sorted)
      {
        /* recurse, now use the built cache */
        ret = tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
      }

      return ret;
    }
    break; /* }}} */
  case TREE_VDB: /* {{{ */
    {
      char           buf[_Q_PATH_MAX * 2];
      struct stat    sb;
      struct dirent *de;
      DIR           *catdir = NULL;
      int            catfd  = -1;
      size_t         len;

      /* this has dirs with name PF, so we can easily populate all pkgs
       * with a single directory traversal */
      len = snprintf(buf, sizeof(buf), "%s/%s", tree->path, cat->name);
      if ((catfd = openat(tree->portroot_fd, buf, O_RDONLY | O_CLOEXEC)) < 0)
        return 0;
      if ((catdir = fdopendir(catfd)) == NULL)
      {
        close(catfd);
        return 0;
      }

      if (filterpn)
      {
        VAL_CLEAR(needle);
        needle.name = query->PN;
        needle.atom = (atom_ctx *)query;
      }

      if (cat->pkgs == NULL)
        cat->pkgs = array_new();

      while (true)
      {
        if ((de = readdir(catdir)) == NULL)
          break;

        if (de->d_name[0] == '.' &&
            (de->d_name[1] == '\0' ||
             (de->d_name[1] == '.' &&
              de->d_name[2] == '\0')))
          continue;

        snprintf(buf + len, sizeof(buf) - len, "/%s", de->d_name);
        if (fstatat(tree->portroot_fd, buf, &sb, 0) < 0 ||
            !S_ISDIR(sb.st_mode))
          continue;

        pkg       = xzalloc(sizeof(*pkg));
        pkg->atom = atom_explode_cat(de->d_name, cat->name);
        pkg->name = xstrdup(pkg->atom->PN);
        pkg->path = xstrdup(buf);
        pkg->cat  = cat;

        array_append(cat->pkgs, pkg);

        if (!sorted &&
            (!filterpn ||
             tree_pkg_compar(&pkg, &nref) == 0))
          ret |= callback(pkg, priv);
      }

      closedir(catdir);

      cat->pkgs_complete = true;

      if (sorted)
      {
        /* recurse, now use the built cache */
        ret = tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
      }

      return ret;
    }
    break; /* }}} */
  default:
    return 0;
  }

  /* unreachable */
  return 0;
}

/* iterates over the given tree, invoking the callback function for
 * packages matching the query, or all when absent
 * the sorted parameter ensures the callback sees packages in order
 * defined by atom_compare */
int tree_foreach_pkg
(
  tree_ctx       *tree,
  tree_pkg_cb     callback,
  void           *priv,
  bool            sorted,
  const atom_ctx *query
)
{
  tree_cat_ctx *cat       = NULL;
  int           ret       = 0;
  bool          filtercat = false;

  /* if we have a query with category, see if we have it already */
  if (query != NULL &&
      query->CATEGORY != NULL)
    filtercat = true;

  if (filtercat &&
      tree->cats != NULL)
  {
    tree_cat_ctx needle;

    VAL_CLEAR(needle);
    needle.name = query->CATEGORY;
    cat = array_binsearch(tree->cats, &needle, tree_cat_compar, NULL);
    if (cat != NULL)
      return tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
    else if (tree->cats_complete)
      return 0;
  }

  /* if our cache is complete, use it, due to above conditions, at this
   * point we either have a filter but no cats (so cannot be complete)
   * or no filter, for which we need all categories to be there */
  if (tree->cats_complete)
  {
    size_t n;

    if (sorted)
      array_sort(tree->cats, tree_cat_compar);

    array_for_each(tree->cats, n, cat)
      ret |= tree_cat_foreach_pkg(cat, callback, priv, sorted, query);

    return ret;
  }

  /* call the tree walker to populate the categories */
  switch (tree->type)
  {
  case TREE_EBUILD:
  case TREE_VDB:
  case TREE_BINPKGS:
    /* {{{ */
    if (filtercat)
    {
      char        buf[_Q_PATH_MAX];
      struct stat sb;

      /* just probe to see if there is a directory named like this */
      snprintf(buf, sizeof(buf), "%s/%s", tree->path, query->CATEGORY);
      VAL_CLEAR(sb);
      if (fstatat(tree->portroot_fd, buf, &sb, 0) < 0 ||
          !S_ISDIR(sb.st_mode))
        return 0;

      cat       = xzalloc(sizeof(*cat));
      cat->name = xstrdup(query->CATEGORY);
      cat->tree = tree;

      if (tree->cats == NULL)
        tree->cats = array_new();
      array_append(tree->cats, cat);

      return tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
    }
    else
    {
      tree_cat_ctx  needle;
      DIR          *dir;
      int           fd;
      bool          domatch = tree->cats != NULL;

      /* loop over directory and populate cache as side-effect, unless
       * sort is requested, then populate first, sort and run from the
       * cache afterwards */
      fd = openat(tree->portroot_fd, tree->path, O_RDONLY | O_CLOEXEC);
      if (fd < 0)
        return 0;
      dir = fdopendir(fd);
      if (dir == NULL)
      {
        close(fd);
        return 0;
      }

      if (tree->cats == NULL)
        tree->cats = array_new();

      VAL_CLEAR(needle);
      while (true)
      {
        struct dirent *de = readdir(dir);

        if (de == NULL)
          break;

        if (tree_filter_cat(de) == 0)
          continue;

        cat = NULL;
        if (domatch)
        {
          needle.name = de->d_name;
          cat = array_binsearch(tree->cats, &needle, tree_cat_compar, NULL);
        }

        if (cat == NULL)
        {
          cat       = xzalloc(sizeof(*cat));
          cat->name = xstrdup(de->d_name);
          cat->tree = tree;
          array_append(tree->cats, cat);
        }

        if (!sorted)
          ret |= tree_cat_foreach_pkg(cat, callback, priv, sorted, query);
      }

      tree->cats_complete = true;

      closedir(dir);

      if (sorted)
      {
        /* recurse, now use the built cache */
        ret = tree_foreach_pkg(tree, callback, priv, sorted, query);
      }

      return ret;
    }
    break; /* }}} */
  case TREE_PACKAGES: /* {{{ */
    {
      tree_pkg_ctx      *pkg      = NULL;
      tree_cat_ctx       needle;
      char              *buf;
      char              *k;
      char              *v;
      char              *cpv;
      char              *nexttok;
      size_t             len;
      size_t             rootlen;
      int                fd;
      bool               eret;

      fd = openat(tree->portroot_fd, tree->path, O_RDONLY | O_CLOEXEC);
      if (fd < 0)
        return 1;

      buf  = NULL;
      len  = 0;
      eret = eat_file_fd(fd, &buf, &len);
      close(fd);

      if (!eret)
        return 1;

      k = strrchr(tree->path, '/');
      if (k != NULL)
        rootlen = k - tree->path;
      else
        rootlen = strlen(tree->path);

      tree->cats = array_new();
      VAL_CLEAR(needle);

      for (k = strtok_r(buf, ":", &nexttok);
           k != NULL;
           k = strtok_r(NULL, ":", &nexttok))
      {
        k = rmspace(k);

        v = strtok_r(NULL, "\n", &nexttok);
        if (v == NULL)
          break;
        v = rmspace(v);

        if (pkg == NULL)
        {
          /* don't attempt to do anything, this is the header which we
           * ignore/not store anything of currently */
        }
        else if (strcmp(k, "CPV") == 0)
        {
          cpv = v;
        }
#define match_key(X) match_key2(X,X)
#define match_key2(X,Y) \
        else if (strcmp(k, #X) == 0) \
          do { \
            if (pkg->meta[Q_##Y] == NULL) \
              pkg->meta[Q_##Y] = xstrdup(v); \
          } while (false)
        match_key(DEFINED_PHASES);
        match_key(DEPEND);
        match_key2(DESC, DESCRIPTION);
        match_key(EAPI);
        match_key(IUSE);
        match_key(KEYWORDS);
        match_key(LICENSE);
        match_key(MD5);
        match_key(SHA1);
        match_key(RDEPEND);
        match_key(SLOT);
        match_key(USE);
        match_key(PDEPEND);
        match_key2(REPO, repository);
        match_key(SIZE);
        match_key(BDEPEND);
        match_key(IDEPEND);
        match_key(PATH);
        match_key(BUILD_ID);
#undef match_key
#undef match_key2

        if (*nexttok == '\n')
        {
          /* double newline, finish a block */
          if (pkg != NULL)
          {
            char pth[_Q_PATH_MAX];
            /* create atom from the path if we have it, else use cpv */
            if (pkg->meta[Q_PATH] != NULL)
            {
              /* construct full path */
              snprintf(pth, sizeof(pth), "%.*s/%s",
                       (int)rootlen, tree->path, pkg->meta[Q_PATH]);

              pkg->path = xstrdup(pth);
              pkg->atom = atom_explode(pkg->meta[Q_PATH]);

              len = strlen(pkg->meta[Q_PATH]);
              if (len > sizeof(".gpkg.tar") - 1 &&
                  memcmp(pkg->meta[Q_PATH] + len - (sizeof(".gpkg.tar") - 1),
                         ".gpkg.tar", sizeof(".gpkg.tar") - 1) == 0)
                pkg->binpkg_gpkg = true;
            }
            else if (cpv != NULL)
            {
              /* this might be an old repo or something, so compute the
               * path assuming it's from the base in PN/PF.tbz2 */
              snprintf(pth, sizeof(pth), "%.*s/%s.tbz2",
                       (int)rootlen, tree->path, cpv);
              pkg->path = xstrdup(pth);
              pkg->atom = atom_explode(cpv);
            }
            else
            {
              /* have no version or anything, skip this */
              tree_pkg_close(pkg);
              cpv = NULL;
              pkg = xzalloc(sizeof(*pkg));
              continue;
            }
            pkg->name = xstrdup(pkg->atom->PN);

            /* BUILD_ID sanity */
            if (pkg->atom->BUILDID == 0 &&
                pkg->meta[Q_BUILD_ID] != NULL)
              pkg->atom->BUILDID = atoi(pkg->meta[Q_BUILD_ID]);

            /* find category for this package */
            needle.name = pkg->atom->CATEGORY;
            cat = array_binsearch(tree->cats, &needle, tree_cat_compar, NULL);

            if (cat == NULL)
            {
              cat = xzalloc(sizeof(*cat));
              cat->name = xstrdup(pkg->atom->CATEGORY);
              cat->tree = tree;
              array_append(tree->cats, cat);
            }
            if (cat->pkgs == NULL)
              cat->pkgs = array_new();

            pkg->cat = cat;
            array_append(cat->pkgs, pkg);
          }

          /* prepare new package */
          cpv = NULL;
          pkg = xzalloc(sizeof(*pkg));
        }
      }

      free(pkg);
      free(buf);

      array_for_each(tree->cats, len, cat)
        cat->pkgs_complete = true;

      tree->cats_complete = true;

      /* ok, now do it for real */
      return tree_foreach_pkg(tree, callback, priv, sorted, query);
    }
    break; /* }}} */
  case TREE_GTREE: /* {{{ */
#ifdef ENABLE_GTREE
    /* we don't optimise anything because reading a single file is fast
     * enough, it just takes some memory, but any retrieval afterwards
     * comes straight from cache */
    if (tree_foreach_pkg_gtree(tree) != 0)
      return 1;
    return tree_foreach_pkg(tree, callback, priv, sorted, query);
#else
    return 0;
#endif
    break; /* }}} */
  default:
    return 0;
  }

  return 0;
}

/* callback for tree_foreach_pkg that appends the pkg to the array given
 * via priv */
static int tree_match_atom_cb
(
  tree_pkg_ctx *pkg,
  void         *priv
)
{
  array *arr = priv;

  array_append(arr, pkg);

  return 0;
}

/* searches the given tree for packages matching the given atom, returns
 * the matching packages, or all when atom is NULL, in an array
 * the returned array contains pointers to tree_pkg_ctx structures
 * backed by the input tree, and as such only the array should be freed,
 * using array_free() */
array *tree_match_atom
(
  tree_ctx       *tree,
  const atom_ctx *atom,
  int             flags
)
{
  array        *ret    = array_new();
  tree_pkg_ctx *w;
  size_t        n;
  bool          sorted = false;

  /* a note on the flags that we control the output results with:
   * - LATEST:  only return the best (latest version) match for each PN
   * - FIRST:   stop searching after the first match (e.g. atom without
   *            category), implies LATEST
   * - VIRTUAL: include the virtual category in results
   * - ACCT:    include the acct-user and acct-group categories in results
   * - SORT:    return the results in sorted order, this is implied by
   *            LATEST and FIRST, and provided because the sort
   *            comparator is not exposed
   * the flags are currently not supported by tree_foreach_pkg, so we
   * manually fixup the array after traversal */

  if (flags & TREE_MATCH_FIRST  ||
      flags & TREE_MATCH_LATEST ||
      flags & TREE_MATCH_SORT   )
    sorted = true;

  tree_foreach_pkg(tree, tree_match_atom_cb, ret, sorted, atom);

  if (!(flags & TREE_MATCH_VIRTUAL))
  {
    array_for_each_rev(ret, n, w)
      if (strcmp(tree_pkg_get_cat_name(w), "virtual") == 0)
        array_remove(ret, n);
  }

  if (!(flags & TREE_MATCH_ACCT))
  {
    array_for_each_rev(ret, n, w)
      if (strncmp("acct-", tree_pkg_get_cat_name(w), sizeof("acct-") - 1) == 0)
        array_remove(ret, n);
  }

  if (flags & TREE_MATCH_FIRST &&
      array_cnt(ret) > 1)
  {
    /* a bit crude, we can optimise this later */
    array *new = array_new();
    array_append(new, array_get(ret, 0));
    array_free(ret);
    ret = new;
  }

  if (flags & TREE_MATCH_LATEST)
  {
    tree_pkg_ctx *suc;

    array_for_each_rev(ret, n, w)
    {
      if (n == 0)
        break;
      suc = array_get(ret, n - 1);
      if (tree_pkg_get_cat_name(suc) == tree_pkg_get_cat_name(w) &&
          strcmp(tree_pkg_atom(suc, false)->PN,
                 tree_pkg_atom(w, false)->PN) == 0)
        array_remove(ret, n);
    }
  }

  return ret;
}

/* reads metadata.xml next to an ebuild and produces a tree_metadata_xml
 * structure */
tree_metadata_xml *tree_pkg_metadata
(
  tree_pkg_ctx *pkg_ctx
)
{
  tree_metadata_xml *ret    = NULL;
  struct stat        s;
  char               buf[_Q_PATH_MAX];
  FILE              *f;
  char              *xbuf;
  char              *p;
  char              *q;
  size_t             len;
  int                fd;

  /* lame @$$ XML parsing, I don't want to pull in a real parser
   * library because we only retrieve one element for now: email
   * technically speaking, email may occur only once in a maintainer
   * tag, but practically speaking we don't care at all, so we can
   * just extract everything between <email> and </email> */

  p   = tree_pkg_get_path(pkg_ctx);
  len = snprintf(buf, sizeof(buf), "%s", p == NULL ? "" : p);
  p   = strrchr(buf, '/');
  if (p != NULL)
    len = p - buf;
  snprintf(buf + len, sizeof(buf) - len, "/metadata.xml");
  fd = openat(tree_pkg_get_portroot_fd(pkg_ctx), buf, O_RDONLY | O_CLOEXEC);

  if (fd == -1)
    return NULL;

  if ((f = fdopen(fd, "r")) == NULL) {
    close(fd);
    return NULL;
  }

  if (fstat(fd, &s) != 0) {
    fclose(f);
    return NULL;
  }

  len = sizeof(*ret) + s.st_size + 1;
  p = xbuf = xmalloc(len);
  if ((off_t)fread(p, 1, s.st_size, f) != s.st_size) {
    free(p);
    fclose(f);
    return NULL;
  }
  p[s.st_size] = '\0';

  ret = xmalloc(sizeof(*ret));
  ret->email = array_new();

  while ((q = strstr(p, "<email>")) != NULL) {
    p = q + sizeof("<email>") - 1;
    if ((q = strstr(p, "</email>")) == NULL)
      break;
    *q = '\0';
    rmspace(p);
    array_append_copy(ret->email, p, strlen(p));
    p = q + 1;
  }

  free(xbuf);
  fclose(f);
  return ret;
}

/* frees up resources used by a tree_metadata_xml structure produced by
 * tree_pkg_metadata */
void tree_close_metadata
(
  tree_metadata_xml *meta_ctx
)
{
  array_deepfree(meta_ctx->email, NULL);
  free(meta_ctx);
}

/* utility function to retrieve the repository name, if defined */
char *tree_get_repo_name
(
  tree_ctx *tree
)
{
  if (tree == NULL)
    return NULL;

  return tree->repo;
}

/* utility function to retrieve the base path of this tree */
char *tree_get_path
(
  tree_ctx *tree
)
{
  if (tree == NULL)
    return NULL;

  return tree->path;
}

/* utility function to retrieve the open filedescriptor to the root used
 * with this tree */
int tree_get_portroot_fd
(
  tree_ctx *tree
)
{
  if (tree == NULL)
    return -1;

  return tree->portroot_fd;
}

/* utility function to retrieve the type of this tree */
enum tree_open_type tree_get_treetype
(
  tree_ctx *tree
)
{
  if (tree == NULL)
    return 0;

  switch (tree->type)
  {
  case TREE_EBUILD:
    return TREETYPE_EBUILD;
  case TREE_VDB:
    return TREETYPE_VDB;
  case TREE_PACKAGES:
  case TREE_BINPKGS:
    return TREETYPE_BINPKG;
  case TREE_GTREE:
    return TREETYPE_GTREE;
  default:
    /* metadata trees should never be top-level trees (we at least
     * cannot create them as such */
    return 0;
  }

  /* unreachable */
}

/* utility function to retrieve the name of the category, without
 * requesting the atom (which may not exist yet) */
char *tree_pkg_get_cat_name
(
  tree_pkg_ctx *pkg
)
{
  if (pkg == NULL)
    return NULL;

  return pkg->cat->name;
}

/* utility function to retrieve the versioned name of the package,
 * without requesting the atom (which may not be necessary depending on
 * the tree type) */
char *tree_pkg_get_pf_name
(
  tree_pkg_ctx *pkg
)
{
  char *ret;

  if (pkg == NULL)
    return NULL;

  /* if the atom is there, always use it, it should never be wrong */
  if (pkg->atom != NULL)
    return pkg->atom->PF;

  switch (pkg->cat->tree->type)
  {
  case TREE_VDB:
  case TREE_GTREE:
    /* the path here is exactly what we're looking for, so a cheap way
     * to retrieve the name */
    ret = strrchr(pkg->path, '/');
    if (ret != NULL)
      ret++;
    else
      ret = pkg->path;
    break;
  default:
    tree_pkg_atom(pkg, false);
    ret = pkg->atom->PF;
    break;
  }

  return ret;
}

/* utility function to retrieve the path to the package including file
 * extensions and leading directories for access or printing */
char *tree_pkg_get_path
(
  tree_pkg_ctx *pkg
)
{
  if (pkg == NULL)
    return NULL;

  return pkg->path;
}

/* utility function to allow callbacks in e.g. tree_foreach_pkg to get
 * the portroot fd to perform openat with the path provided by the
 * package */
int tree_pkg_get_portroot_fd
(
  tree_pkg_ctx *pkg
)
{
  if (pkg == NULL)
    return -1;
  
  return pkg->cat->tree->portroot_fd;
}

/* utility function to retrieve the treetype for this package as
 * introspection for e.g. when multiple trees are combined in results
 * (think of vdb, binpkg and ebuild trees in resolving situations) */
enum tree_open_type tree_pkg_get_treetype
(
  tree_pkg_ctx *pkg
)
{
  if (pkg == NULL)
    return 0;  /* invalid */

  return tree_get_treetype(pkg->cat->tree);
}

/* utility function to retrieve the associated tree to this pkg */
tree_ctx *tree_pkg_get_tree
(
  tree_pkg_ctx *pkg
)
{
  if (pkg == NULL)
    return NULL;

  return pkg->cat->tree;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
