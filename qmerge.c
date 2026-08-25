/*
 * Copyright 2005-2026 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <xalloc.h>
#include <fnmatch.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>

#ifdef ENABLE_GPKG
# include <archive.h>
# include <archive_entry.h>
#endif

#include "stat-time.h"

#include "atom.h"
#include "contents.h"
#include "copy_file.h"
#include "dep.h"
#include "eat_file.h"
#include "file_magic.h"
#include "hash.h"
#include "human_readable.h"
#include "move_file.h"
#include "profile.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xasprintf.h"
#include "xchdir.h"
#include "xmkdir.h"
#include "xpak.h"
#include "xsystem.h"

#ifndef GLOB_BRACE
# define GLOB_BRACE     (1 << 10) /* Expand "{a,b}" to "a" "b".  */
#endif

/*
   --nofiles                        don't verify files in package
   --noscript                       don't execute pkg_{pre,post}{inst,rm} (if any)
   */

/* How things should work, ideally.  This is not how it currently is
 * implemented at all.
 *
 * invocation: qmerge ... pkg/a pkg/b pkg/c
 * initial mergeset: pkg/a pkg/b pkg/c
 * resolving:
 * - for pkg, get dependencies
 *   * apply masks
 *   * extract depend, rdepend, bdepend, etc.
 *   * filter flags in use (libq/dep)
 *   * add found pkgs to mergeset if not present in resolvedset
 * - while mergeset contains pkgs
 *   * resolve pkg (see above)
 *   * move from mergeset to resolvedset
 * here (if all is well) mergeset is empty, and resolvedset contains the
 * pkgs to be installed.  If for instance pkg/c depends on pkg/b it
 * won't occur double, for it is a set.
 *
 * Technically, because we deal with binpkgs, we don't have
 * dependencies, but there can be pre/post scripts that actually use
 * depended on pkgs, and if we would invoke ebuild(5) to compile instead
 * of unpack, we would respect the order too, so the resolvedset here
 * needs to be ordered into a list.
 * While functionally one can re-evaluate the dependencies here,
 * implementation wise, it probably is easier to build the final merge
 * order list while resolving.  This can also add the additional
 * metadata of whether a pkg was requested (cmdline) or pulled in a dep,
 * and for which package.  That information can be leveraged to draw a
 * tree (visuals) but also to determine possible parallel installation
 * paths.
 *
 * For example, original invocation could lead to a resolved merge list
 * of:
 *   M pkg/a
 *   m  pkg/d
 *   R pkg/c
 *   M  pkg/b
 *
 * After this, the pkgs need to be fetched and the pre phases need to be
 * run.  In interactive mode, a question probably needs to be inserted
 * after the printing of the resolved merge list.  Then if all checks
 * out, the unpack and merge to live fs + vdb updates can be performed.
 * Ideally the unpack (or compile via ebuild(5)) phase is separated from
 * the final merge to live fs.  The latter always has to be serial, but
 * the former can run in parallel based on dependencies seen.  For
 * our example, pkg/d and pkg/b can be unpacked in parallel, merged in
 * which order finished first, then pkg/a and pkg/c can commence.  So
 * the start set is pkg/d and pkg/b, and each unlocks pkg/a and pkg/c
 * respectively, which are not constrained, other than the final merge
 * logic.
 *
 * Errors
 * There are multiple kinds of errors in multiple stages.  Whether they
 * are fatal depends on a number of factors.
 * - resolution error
 *   Failing to resolve an atom basically makes the tree that that pkg
 *   is part of unmergable; that is, it needs to be discarded from the
 *   workset.  In interactive mode after resolving we could ask if the
 *   user wants to continue (if there's anything else we *can* do),
 *   non-interactive we could decide to just go ahead with whatever we
 *   was possible, unless we add a flag that stops at resolution errors.
 * - USE-dep resolution error
 *   The state of USE-flags must be respected, and can cause problems,
 *   in particular cyclic dependencies.  Solution to those is to disable
 *   USE-flags temporary and re-merge without.  For now, these errors
 *   are not resolved, but should be detected and treated as resolution
 *   errors above.
 * - fetch error
 *   Either because fetching the binpkg or the source files fails.  This
 *   discards the atom and its tree.  It may be possible in this case to
 *   try and re-resolve using an older version of the pkg.  But since
 *   this kind of error is pretty much in the foundation of the work, it
 *   seems more logical to exclude the tree the package belongs too,
 *   because at this point parallel execution happens, it makes no sense
 *   any more to ask the user to abort.
 * - unpack or merge error
 *   Under these errors are the failures in the various pkg checks (run
 *   phases) and for source-based installs obviously compilation
 *   failures.  These discard an entire tree, and like fetch errors,
 *   we don't have a clear opportunity anymore to ask whether or not to
 *   continue.
 * - live fs + vdb error
 *   This should be rare, but most probably filesystem related errors,
 *   such as running out of diskspace or lacking certain permissions.
 *   Corrupting the VDB hopefully doesn't happen, but it is possible to
 *   encounter problems there as well.  Like fetch and unpack errors, we
 *   should try to continue with whatever we can, but will not roll-back
 *   already merged packages.  So a failure here, should result in
 *   dropping all children from the failed pkg.
 *
 * After merging qlop -Ev should show whatever was merged successfully,
 * so qmerge should show what failed to merge (in what stage).
 */

typedef enum qmerge_node_type {
  NTYPE_ROOT = 0,   /* pseudo top-level node, should only be one */
  NTYPE_MERGE,
  NTYPE_UNMERGE,
  NTYPE_REMERGE,
  NTYPE_UPGRADE,
  NTYPE_DOWNGRADE,
  NTYPE_INSTALLED,
  NTYPE_MASKED,
  NTYPE_CONFLICT,
  NTYPE_AMBIGIOUS,
  NTYPE_ERROR,
  NTYPE_IGNORE
} node_type_t;

typedef struct qmerge_node_ node_t;
struct qmerge_node_ {
  tree_pkg_ctx *pkg;       /* tree handle + atom, etc. if resolved */
  tree_pkg_ctx *ipkg;      /* VDB pkg, if present */
  atom_ctx     *atom;      /* input atom */
  array        *parents;   /* ptrs to parents, always set */
  array        *postdeps;  /* ptrs to nodes, for convenience always set */
  array        *predeps;   /* idem, r+p are post, i is pre this node */
  dep_node_t   *dep;       /* dep in case of resolving error NTYPE_ERROR */
  array        *pkgs;      /* array to be freed with pkgs for NTYPE_AMBIGIOUS */
  char         *mask;      /* string form of mask reason */
  node_type_t   type;
  bool          is_arg:1;  /* node was given by user */
  bool          found:1;   /* for NTYPE_ERROR */
};


#define QMERGE_FLAGS "fFskKUpuyO" COMMON_FLAGS
static struct option const qmerge_long_opts[] = {
  {"fetch",   no_argument, NULL, 'f'},
  {"force",   no_argument, NULL, 'F'},
  {"search",  no_argument, NULL, 's'},
  {"install", no_argument, NULL, 'K'},   /* legacy */
  {"usepkgonly", no_argument, NULL, 'K'},  /* emerge */
  {"unmerge", no_argument, NULL, 'U'},
  {"pretend", no_argument, NULL, 'p'},
  {"keepwork",no_argument, NULL, 127},
  {"update",  no_argument, NULL, 'u'},
  {"yes",     no_argument, NULL, 'y'},
  {"nodeps",  no_argument, NULL, 'O'},
  {"debug",   no_argument, NULL, 128},
  COMMON_LONG_OPTS
};
static const char * const qmerge_opts_help[] = {
  "Fetch package and newest Packages metadata",
  "Fetch package (skipping Packages)",
  "Search available packages",
  "Alias for --usepkgonly",
  "Install only from binary packages",
  "Uninstall package",
  "Pretend only",
  "Do not cleanup the unpacked binpkgs in qmerge tempdir",
  "Update only",
  "Don't prompt before overwriting",
  "Don't merge dependencies",
  "Run shell funcs with `set -x`",
  COMMON_OPTS_HELP
};
#define qmerge_usage(ret) usage(ret, QMERGE_FLAGS, qmerge_long_opts, qmerge_opts_help, NULL, lookup_applet_idx("qmerge"))

char interactive = 1;
char install = 0;
char uninstall = 0;
char force_download = 0;
char follow_rdepends = 1;
char qmerge_strict = 0;
char update_only = 0;
bool keep_work = false;
bool debug = false;
const char Packages[] = "Packages";

static void pkg_fetch(int, const depend_atom *, tree_pkg_ctx *);

static bool qmerge_prompt
(
  const char *p
)
{
  printf("%s%s?%s [%sY%s/%sn%s] ", BOLD, p, NORM, GREEN, NORM, RED, NORM);
  fflush(stdout);
  switch (fgetc(stdin))
  {
  case '\n':
  case 'y':
  case 'Y':
    return true;
  default:
    return false;
  }
}

static void fetch
(
  const char *destdir,
  const char *src
)
{
  if (!binhost[0])
    return;

  fflush(NULL);

#if 0
  if (getenv("FETCHCOMMAND") != NULL)
  {
    char buf[BUFSIZ];
    snprintf(buf, sizeof(buf), "(export DISTDIR='%s' URI='%s/%s'; %s)",
             destdir, binhost, src, getenv("FETCHCOMMAND"));
    xsystem(buf, AT_FDCWD);
  }
  else
#endif
  {
    char *path = NULL;

    /* wget -c -q -P <dir> <uri> */
    const char *argv[] = {
      "echo",
      "wget",
      "-c",
      "-P",
      destdir,
      path,
      quiet ? (char *)"-q" : NULL,
      NULL,
    };

    xasprintf(&path, "%s/%s", binhost, src);

    if (!pretend && (force_download || install))
      xsystemv(&argv[1], AT_FDCWD);  /* skip echo */
    else
      xsystemv(argv, AT_FDCWD);

    free(path);
  }

  fflush(stdout);
  fflush(stderr);
}

static int config_protected
(
  const char *buf,
  int         cp_argc,
  char      **cp_argv,
  int         cpm_argc,
  char      **cpm_argv
)
{
  int i;

  /* Check CONFIG_PROTECT_MASK */
  for (i = 1; i < cpm_argc; ++i)
    if (strncmp(cpm_argv[i], buf, strlen(cpm_argv[i])) == 0)
      return 0;

  /* Check CONFIG_PROTECT */
  for (i = 1; i < cp_argc; ++i)
    if (strncmp(cp_argv[i], buf, strlen(cp_argv[i])) == 0)
      return 1;

  /* this would probably be bad */
  if (strcmp(CONFIG_EPREFIX "bin/sh", buf) == 0)
    return 1;

  return 0;
}

static void crossmount_rm
(
  const char               *fname,
  const struct stat * const st,
  int                       fd,
  char                     *qpth
)
{
  struct stat lst;

  if (fstatat(fd, fname, &lst, AT_SYMLINK_NOFOLLOW) == -1)
    return;
  if (lst.st_dev != st->st_dev)
  {
    warn("skipping crossmount install masking: %s", fname);
    return;
  }
  qprintf("%s<<<%s %s/%s (INSTALL_MASK)\n", YELLOW, NORM, qpth, fname);
  rm_rf_at(fd, fname);
}

enum inc_exc {
  INCLUDE = 1,
  EXCLUDE = 2
};

static void install_mask_check_dir
(
  char                   ***maskv,
  int                       maskc,
  const struct stat * const st,
  int                       fd,
  ssize_t                   level,
  enum inc_exc              parent_mode,
  char                     *qpth
)
{
  struct stat     s;
  struct dirent **files;
  char           *npth       = qpth + strlen(qpth);
  int             cnt;
  int             i;
  int             j;
  enum inc_exc    mode;
  enum inc_exc    child_mode;

  cnt = scandirat(fd, ".", &files, filter_self_parent, alphasort);
  for (j = 0; j < cnt; j++)
  {
    mode = child_mode = parent_mode;
    for (i = 0; i < maskc; i++)
    {
      if ((ssize_t)maskv[i][0] < 0)
      {
        /* relative matches need to be a "file", as the Portage
         * implementation suggests, so that's easy for us here,
         * since we can just match it against each component in
         * the path */
        if ((ssize_t)maskv[i][0] < -1)
          continue;  /* this is unsupported, so skip it */
        /* this also works if maskv happens to be a glob */
        if (fnmatch(maskv[i][1], files[j]->d_name, FNM_PERIOD) != 0)
          continue;
        mode = child_mode = maskv[i][2] ? INCLUDE : EXCLUDE;
      }
      else if ((ssize_t)maskv[i][0] < level)
      {
        /* either this is a mask that didn't match, or it
         * matched, but a negative match exists for a deeper
         * level, parent_mode should reflect this */
        continue;
      }
      else
      {
        if (fnmatch(maskv[i][level], files[j]->d_name, FNM_PERIOD) != 0)
          continue;

        if ((ssize_t)maskv[i][0] == level)  /* full mask match */
          mode = child_mode =
            (ssize_t)maskv[i][level + 1] ? INCLUDE : EXCLUDE;
        else if (maskv[i][(ssize_t)maskv[i][0] + 1])
          /* partial include mask */
          mode = INCLUDE;
      }
    }

    DBG("%s/%s: %s/%s", qpth, files[j]->d_name,
        mode == EXCLUDE       ? "EXCLUDE" : "INCLUDE",
        child_mode == EXCLUDE ? "EXCLUDE" : "INCLUDE");
    if (mode == EXCLUDE)
    {
      crossmount_rm(files[j]->d_name, st, fd, qpth);
      continue;
    }

    if (fstatat(fd, files[j]->d_name, &s, AT_SYMLINK_NOFOLLOW) != 0)
      continue;
    if (S_ISDIR(s.st_mode))
    {
      int subfd = openat(fd, files[j]->d_name, O_RDONLY);
      if (subfd < 0)
        continue;
      snprintf(npth, _Q_PATH_MAX - (npth - qpth), "/%.*s",
               (int)(_Q_PATH_MAX - (npth - qpth)), files[j]->d_name);
      install_mask_check_dir(maskv, maskc, st, subfd,
                             level + 1, child_mode, qpth);
      close(subfd);
      *npth = '\0';
    }
  }
  scandir_free(files, cnt);
}

static void install_mask_pwd
(
  int                       iargc,
  char                    **iargv,
  const struct stat * const st,
  int                       fd
)
{
  char     qpth[_Q_PATH_MAX];
  char    *p;
  char    *q;
  size_t   maxdirs;
  char   **masks;
  size_t   masksc;
  char  ***masksv;
  size_t   cnt;
  int      i;

  /* we have to deal with "negative" masks, see
   * https://archives.gentoo.org/gentoo-portage-dev/message/29e128a9f41122fa0420c1140f7b7f94
   * which means we'll need to see what thing matches last
   * (inclusion or exclusion) for *every* file :( */

   /*
    example package contents:
    /e/t1
    /u/b/t1
    /u/b/t2
    /u/l/lt1
    /u/s/d/t1
    /u/s/m/m1/t1
    /u/s/m/m5/t2

    masking rules:     array encoding:
     /u/s              2 u s 0          relative=0 include=0
    -/u/s/m/m1         4 u s m m1 1     relative=0 include=1
     e                -1 e 0            relative=1 include=0

    should result in:
    /u/b/t1
    /u/b/t2
    /u/l/lt1
    /u/s/m/m1/t1
    strategy:
    - for each dir level
      - find if there is a match on that level in rules
      - if the last match is the full mask
        - if the mask is negated, do not remove entry
        - else, remove entry
      - if the last match is negated, partial and a full mask matched before
        - do not remove entry
    practice:
    /e | matches "e" -> remove
    /u | matches partial last negated -> continue
      /b | doesn't match -> leave subtree
      /l | doesn't match -> leave subtree
      /s | match, partial last negated match -> remember match, continue
        /d | doesn't match -> remembered match, remove subtree
        /m | partial match negated -> continue
          /m1 | match negated -> leave subtree
          /m5 | doesn't match -> remembered match, remove subtree
    */

  /* find the longest path so we can allocate a matrix */
  maxdirs = 0;
  for (i = 1; i < iargc; i++)
  {
    char lastc = '/';

    cnt = 1; /* we always have "something", right? */
    p = iargv[i];
    if (*p == '-')
      p++;
    for (; *p != '\0'; p++)
    {
      /* eliminate duplicate /-es, also ignore the leading / in
       * the count */
      if (*p == '/' && *p != lastc)
        cnt++;
      lastc = *p;
    }
    if (cnt > maxdirs)
      maxdirs = cnt;
  }
  maxdirs += 2;  /* allocate plus relative and include elements */

  /* allocate and populate matrix */
  masksc = iargc - 1;
  masks  = xmalloc(sizeof(char *) * (maxdirs * masksc));
  masksv = xmalloc(sizeof(char **) * (masksc));
  for (i = 1; i < iargc; i++)
  {
    masksv[i - 1] = &masks[(i - 1) * maxdirs];
    p             = iargv[i];
    cnt           = 1;  /* first level is reserved for count */

    /* ignore include marker */
    if (*p == '-')
      p++;
    for (q = p; *p != '\0'; p++)
    {
      if (*p == '/')
      {
        /* make new entry if non-zero (such as at the start of
         * the path) */
        if (q != p)
        {
          masks[((i - 1) * maxdirs) + cnt] = q;
          cnt++;
        }

        /* terminate part and fold duplicate slashes */
        do
        {
          if (cnt == 1)  /* retain / at start of iargv[i] */
            p++;
          else
            *p++ = '\0';
        }
        while (*p == '/');
        if (*p == '\0')
          break;

        /* start new entry */
        q = p;
      }
    }
    /* write final component, if any */
    if (q != p)
      masks[((i - 1) * maxdirs) + cnt] = q;

    /* brute force cast below values, a pointer hopefully is size_t,
     * which is large enough to store what we need here */
    p = iargv[i];
    /* set include bit */
    if (*p == '-')
    {
      masks[((i - 1) * maxdirs) + cnt + 1] = (char *)1;
      p++;
    }
    else
    {
      masks[((i - 1) * maxdirs) + cnt + 1] = (char *)0;
    }
    /* set count */
    masks[((i - 1) * maxdirs) + 0] =
      (char *)((*p == '/' ? 1 : -1) * cnt);
  }

#if EBUG
  fprintf(warnout, "applying install masks:\n");
  for (cnt = 0; cnt < masksc; cnt++)
  {
    ssize_t plen = (ssize_t)masksv[cnt][0];
    fprintf(warnout, "%3zd  ", plen);
    if (plen < 0)
      plen = -plen;
    for (i = 1; i <= plen; i++)
      fprintf(warnout, "%s ", masksv[cnt][i]);
    fprintf(warnout, " %zd\n", (size_t)masksv[cnt][i]);
  }
#endif

  cnt = snprintf(qpth, _Q_PATH_MAX, "%s", CONFIG_EPREFIX);
  cnt--;
  if (qpth[cnt] == '/')
    qpth[cnt] = '\0';

  install_mask_check_dir(masksv, masksc, st, fd, 1, INCLUDE, qpth);

  free(masks);
  free(masksv);
}

/* PMS 9.2 Call order */
enum pkg_phases {
  PKG_PRETEND  = 1,
  PKG_SETUP    = 2,
  /* skipping src_* */
  PKG_PREINST  = 3,
  PKG_POSTINST = 4,
  PKG_PRERM    = 5,
  PKG_POSTRM   = 6
};
#define MAX_EAPI  8
static struct {
  enum pkg_phases phase;
  const char     *phasestr;
  unsigned char   eapi[1 + MAX_EAPI];
} phase_table[] = {
  { 0,            NULL,           {0,0,0,0,0,0,0,0,0} },   /* align */
  /* phase                   EAPI: 0 1 2 3 4 5 6 7 8 */
  { PKG_PRETEND,  "pkg_pretend",  {0,0,0,0,1,1,1,1,1} },   /* table 9.3 */
  { PKG_SETUP,    "pkg_setup",    {1,1,1,1,1,1,1,1,1} },
  { PKG_PREINST,  "pkg_preinst",  {1,1,1,1,1,1,1,1,1} },
  { PKG_POSTINST, "pkg_postinst", {1,1,1,1,1,1,1,1,1} },
  { PKG_PRERM,    "pkg_prerm",    {1,1,1,1,1,1,1,1,1} },
  { PKG_POSTRM,   "pkg_postrm",   {1,1,1,1,1,1,1,1,1} }
};
static struct {
  enum pkg_phases phase;
  const char     *varname;
} phase_replacingvers[] = {
  { 0,            NULL                  },   /* align */
  /* phase        varname                  PMS 11.1.2 */
  { PKG_PRETEND,  "REPLACING_VERSIONS"  },
  { PKG_SETUP,    "REPLACING_VERSIONS"  },
  { PKG_PREINST,  "REPLACING_VERSIONS"  },
  { PKG_POSTINST, "REPLACING_VERSIONS"  },
  { PKG_PRERM,    "REPLACED_BY_VERSION" },
  { PKG_POSTRM,   "REPLACED_BY_VERSION" }
};

static void pkg_run_func_at
(
  int             dirfd,
  const char     *vdb_path,
  const char     *phases,
  enum pkg_phases phaseidx,
  const char     *D,
  const char     *T,
  const char     *EAPI,
  const char     *replacing
)
{
  const char *func;
  const char *phase;
  char       *script;
  int         eapi;

  /* EAPI officially is a string, but since the official ones are only
   * numbers, we'll just go with the numbers */
  eapi = (int)strtol(EAPI, NULL, 10);
  if (eapi > MAX_EAPI)
    eapi = MAX_EAPI;  /* let's hope latest known EAPI is closest */

  /* see if this function should be run for the EAPI */
  if (!phase_table[phaseidx].eapi[eapi])
    return;

  /* This assumes no func is a substring of another func.
   * Today, that assumption is valid for all funcs ...
   * The phases are the func with the "pkg_" chopped off. */
  func = phase_table[phaseidx].phasestr;
  phase = func + 4;
  if (strstr(phases, phase) == NULL)
  {
    qprintf("--- %s\n", func);
    return;
  }

  qprintf("@@@ %s\n", func);

  xasprintf(&script,
            /* Provide funcs required by the PMS */
            "EBUILD_PHASE=%3$s\n"
            "debug-print() { :; }\n"
            "debug-print-function() { :; }\n"
            "debug-print-section() { :; }\n"
            /* Not quite right */
            "has_version() { [ -n \"$(qlist -ICqe \"$1\")\" ]; }\n"
            "best_version() { qlist -ICqev \"$1\"; }\n"
            "use() { useq \"$@\"; }\n"
            "usex() { useq \"$1\" && echo \"${2-yes}$4\" || echo \"${3-no}$5\"; }\n"
            "useq() { hasq \"$1\" ${USE}; }\n"
            "usev() { hasv \"$1\" ${USE}; }\n"
            "has() { hasq \"$@\"; }\n"
            "hasq() { local h=$1; shift; case \" $* \" in *\" $h \"*) return 0;; *) return 1;; esac; }\n"
            "hasv() { hasq \"$@\" && echo \"$1\"; }\n"
            "elog() { printf ' * %%b\\n' \"$*\" >&2; }\n"
            "einfon() { printf ' * %%b' \"$*\" >&2; }\n"
            "einfo() { elog \"$@\"; }\n"
            "ewarn() { elog \"$@\"; }\n"
            "eqawarn() { elog \"QA: \"\"$@\"; }\n"
            "eerror() { elog \"$@\"; }\n"
            "die() { eerror \"$@\"; exit 1; }\n"
            "fowners() { local f a=$1; shift; for f in \"$@\"; do chown $a \"${ED}/${f}\"; done; }\n"
            "fperms() { local f a=$1; shift; for f in \"$@\"; do chmod $a \"${ED}/${f}\"; done; }\n"
            /* TODO: This should suppress `die` */
            "nonfatal() { \"$@\"; }\n"
            "ebegin() { printf ' * %%b ...' \"$*\" >&2; }\n"
            "eend() { local r=${1:-$?}; [ $# -gt 0 ] && shift; [ $r -eq 0 ] && echo ' [ ok ]' || echo \" $* \"'[ !! ]'; return $r; } >&2\n"
            "dodir() { mkdir -p \"$@\"; }\n"
            "keepdir() { dodir \"$@\" && touch \"$@\"/.keep_${CATEGORY}_${PN}-${SLOT%%/*}; }\n"
            /* TODO: This should be fatal upon error */
            "emake() { ${MAKE:-make} ${MAKEOPTS} \"$@\"; }\n"
            /* Unpack the env */
            "{ mkdir -p \"%6$s\"; "
            "bzip2 -dc '%1$s/environment.bz2' > \"%6$s/environment\" "
            "|| exit 1; }\n"
            /* Load the main env */
            ". \"%6$s/environment\"\n"
            /* Reload env vars that matter to us */
            "export EBUILD_PHASE_FUNC='%2$s'\n"
            "export FILESDIR=/.does/not/exist/anywhere\n"
            "export MERGE_TYPE=binary\n"
            "export ROOT='%4$s'\n"
            "export EROOT=\"${ROOT%%/}${EPREFIX%%/}/\"\n"
            /* BROOT, SYSROOT, ESYSROOT: PMS table 8.3 Prefix values for DEPEND */
            "export BROOT=\n"
            "export SYSROOT=\"${ROOT}\"\n"
            "export ESYSROOT=\"${EROOT}\"\n"
            "export D=\"%5$s\"\n"
            "export ED=\"${D%%/}${EPREFIX%%/}/\"\n"
            "export T=\"%6$s\"\n"
            /* we do not support preserve-libs yet, so force
             * preserve_old_lib instead */
            "export FEATURES=\"${FEATURES/preserve-libs/}\"\n"
            /* replacing versions: we ignore EAPI availability, for it will
             * never hurt */
            "export %7$s=\"%8$s\"\n"
            /* Finally run the func */
            "%9$s%2$s\n"
            /* Ignore func return values (not exit values) */
            ":",
    /*1*/ vdb_path,
    /*2*/ func,
    /*3*/ phase,
    /*4*/ portroot,
    /*5*/ D,
    /*6*/ T,
    /*7*/ phase_replacingvers[phaseidx].varname,
    /*8*/ replacing,
    /*9*/ debug ? "set -x;" : "");
  xsystem(script, dirfd);
  free(script);
}
#define pkg_run_func(...) pkg_run_func_at(AT_FDCWD, __VA_ARGS__)

/* Copy one tree (the single package) to another tree (ROOT) */
static int merge_tree_at
(
  int         fd_src,
  const char *src,
  int         fd_dst,
  const char *dst,
  FILE       *contents,
  size_t      eprefix_len,
  set       **objs,
  char      **cpathp,
  int         cp_argc,
  char      **cp_argv,
  int         cpm_argc,
  char      **cpm_argv
)
{
  struct stat    st;
  DIR           *dir;
  struct dirent *de;
  char          *cpath;
  size_t         clen;
  size_t         nlen;
  size_t         mnlen;
  int            i;
  int            ret;
  int            subfd_src;
  int            subfd_dst;

  ret = -1;

  /* Get handles to these subdirs */
  /* Cannot use O_PATH as we want to use fdopendir() */
  subfd_src = openat(fd_src, src, O_RDONLY|O_CLOEXEC);
  if (subfd_src < 0)
    return ret;
  subfd_dst = openat(fd_dst, dst, O_RDONLY|O_CLOEXEC|O_PATH);
  if (subfd_dst < 0)
  {
    close(subfd_src);
    return ret;
  }

  i = dup(subfd_src);  /* fdopendir closes its argument */
  dir = fdopendir(i);
  if (!dir)
    goto done;

  cpath       = *cpathp;
  clen        = strlen(cpath);
  cpath[clen] = '/';
  nlen        = 0;
  mnlen       = 0;

  while ((de = readdir(dir)) != NULL)
  {
    const char *name = de->d_name;

    if (filter_self_parent(de) == 0)
      continue;

    /* Build up the full path for this entry */
    nlen = strlen(name);
    if (mnlen < nlen)
    {
      cpath = *cpathp = xrealloc(*cpathp, clen + 1 + nlen + 1);
      mnlen = nlen;
    }
    strcpy(cpath + clen + 1, name);

    /* Find out what the source path is */
    if (fstatat(subfd_src, name, &st, AT_SYMLINK_NOFOLLOW))
    {
      warnp("could not read %s", cpath);
      continue;
    }

    /* Migrate a directory */
    if (S_ISDIR(st.st_mode))
    {
      if (!pretend &&
          mkdirat(subfd_dst, name, st.st_mode))
      {
        if (errno != EEXIST) {
          warnp("could not create %s", cpath);
          continue;
        }

        /* XXX: update times of dir ? */
      }

      /* syntax: dir dirname */
      if (!pretend)
        fprintf(contents, "dir %s\n", cpath);
      *objs = add_set(cpath, *objs);
      qprintf("%s---%s %s%s%s/\n", GREEN, NORM, DKBLUE, cpath, NORM);

      /* Copy all of these contents */
      merge_tree_at(subfd_src, name,
                    subfd_dst, name, contents, eprefix_len,
                    objs, cpathp, cp_argc, cp_argv, cpm_argc, cpm_argv);
      cpath = *cpathp;
      mnlen = 0;

      /* In case we didn't install anything, prune the empty dir */
      if (!pretend)
        unlinkat(subfd_dst, name, AT_REMOVEDIR);
    }
    else if (S_ISREG(st.st_mode))
    {
      /* Migrate a file */
      char       *hash;
      const char *dname;
      char        buf[_Q_PATH_MAX * 2];
      struct stat ignore;

      /* syntax: obj filename hash mtime */
      hash = hash_file_at(subfd_src, name, HASH_MD5);
      if (!pretend)
        fprintf(contents, "obj %s %s %zu""\n",
                cpath, hash ? hash : "xxx", (size_t)st.st_mtime);

      /* Check CONFIG_PROTECT */
      if (config_protected(cpath + eprefix_len,
                           cp_argc, cp_argv, cpm_argc, cpm_argv) &&
          fstatat(subfd_dst, name, &ignore, AT_SYMLINK_NOFOLLOW) == 0)
      {
        /* ._cfg####_ */
        char *num;
        dname = buf;
        snprintf(buf, sizeof(buf), "._cfg####_%s", name);
        num = buf + 5;
        for (i = 0; i < 10000; i++)
        {
          sprintf(num, "%04i", i);
          num[4] = '_';
          if (fstatat(subfd_dst, dname, &ignore, AT_SYMLINK_NOFOLLOW))
            break;
        }
        qprintf("%s>>>%s %s (%s)\n", GREEN, NORM, cpath, dname);
      }
      else
      {
        dname = name;
        qprintf("%s>>>%s %s\n", GREEN, NORM, cpath);
      }
      *objs = add_set(cpath, *objs);

      if (pretend)
        continue;

      if (move_file(subfd_src, name, subfd_dst, dname, &st) != 0)
        warnp("failed to move file from %s", cpath);
    }
    else if (S_ISLNK(st.st_mode))
    {
      /* Migrate a symlink */
      struct timespec times[2];
      char            sym[_Q_PATH_MAX];
      size_t          len = st.st_size;

      /* Find out what we're pointing to */
      if (readlinkat(subfd_src, name, sym, sizeof(sym)) == -1)
      {
        warnp("could not read link %s", cpath);
        continue;
      }
      sym[len < _Q_PATH_MAX ? len : _Q_PATH_MAX - 1] = '\0';

      /* syntax: sym src -> dst mtime */
      if (!pretend)
        fprintf(contents, "sym %s -> %s %zu\n",
                cpath, sym, (size_t)st.st_mtime);
      qprintf("%s>>>%s %s%s -> %s%s\n", GREEN, NORM,
              CYAN, cpath, sym, NORM);
      *objs = add_set(cpath, *objs);

      if (pretend)
        continue;

      /* Make it in the dest tree */
      if (symlinkat(sym, subfd_dst, name))
      {
        /* If the symlink exists, unlink it and try again */
        if (errno != EEXIST ||
            unlinkat(subfd_dst, name, 0) ||
            symlinkat(sym, subfd_dst, name))
        {
          warnp("could not create link %s to %s", cpath, sym);
          continue;
        }
      }

      times[0] = get_stat_atime(&st);
      times[1] = get_stat_mtime(&st);
      utimensat(subfd_dst, name, times, AT_SYMLINK_NOFOLLOW);
    }
    else
    {
      /* WTF is this !? a door? */
      warnp("unknown file type %s", cpath);
      continue;
    }
  }

  closedir(dir);
  ret = 0;

done:
  close(subfd_src);
  close(subfd_dst);

  return ret;
}

static void pkg_extract_xpak_cb
(
  void *ctx,
  char *pathname,
  int   pathname_len,
  int   data_offset,
  int   data_len,
  char *data
)
{
  FILE *out;
  int  *destdirfd = ctx;
  (void)pathname_len;

  int fd = openat(*destdirfd, pathname,
                  O_WRONLY | O_CLOEXEC | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return;
  out = fdopen(fd, "w");
  if (!out)
    return;

  fwrite(data + data_offset, 1, data_len, out);

  fclose(out);
}

static int pkg_unmerge
(
  tree_pkg_ctx *pkg_ctx,
  depend_atom  *rpkg,
  set_t        *keep,
  int           cp_argc,
  char        **cp_argv,
  int           cpm_argc,
  char        **cpm_argv
)
{
  char      T[_Q_PATH_MAX];
  atom_ctx *atom = tree_pkg_atom(pkg_ctx, false);
  array    *dirs = array_new();
  char     *phases;
  char     *eprefix;
  char     *dir;
  char     *contentsp;
  char     *buf;
  char     *savep;
  size_t    eprefix_len;
  size_t    n;
  int       portroot_fd;
  bool      unmerge_config_protected;

  buf = phases = NULL;
  snprintf(T, sizeof(T), "%s%s/qmerge._unmerge_.%s",
           portroot, port_tmpdir, atom->PF);

  printf("%s***%s unmerging %s\n", YELLOW, NORM,
         atom_format("%[CATEGORY]%[PF]", atom));

  portroot_fd = tree_pkg_get_portroot_fd(pkg_ctx);

  /* execute the pkg_prerm step if we're just unmerging, not when
   * replacing, pkg_merge will have called prerm right before merging
   * the replacement package */
  if (!pretend &&
      rpkg == NULL)
  {
    buf = tree_pkg_meta(pkg_ctx, Q_EAPI);
    if (buf == NULL)
      buf = (char *)"0";  /* default */
    phases = tree_pkg_meta(pkg_ctx, Q_DEFINED_PHASES);
    if (phases != NULL) {
      mkdir_p(T, 0755);
      pkg_run_func_at(portroot_fd, tree_pkg_get_path(pkg_ctx),
                      phases, PKG_PRERM,
                      T, T, buf, "");
    }
  }

  eprefix = tree_pkg_meta(pkg_ctx, Q_EPREFIX);
  if (eprefix == NULL)
    eprefix_len = 0;
  else
    eprefix_len = strlen(eprefix);

  unmerge_config_protected =
    contains_set("config-protect-if-modified", features);

  /* get a handle on the things to clean up */
  contentsp = tree_pkg_meta(pkg_ctx, Q_CONTENTS);
  if (contentsp == NULL)
    return 1;
  contentsp = xstrdup(contentsp);  /* should not modify pkg_ctx */

  for (buf = strtok_r(contentsp, "\n", &savep);
       buf != NULL;
       buf = strtok_r(NULL, "\n", &savep))
  {
    bool            del;
    contents_entry *e;
    char            zing[20];
    int             protected = 0;
    struct stat     st;

    e = contents_parse_line(buf);
    if (!e)
      continue;

    protected = config_protected(e->name + eprefix_len,
                                 cp_argc, cp_argv, cpm_argc, cpm_argv);

    /* This should never happen ... */
    assert(e->name[0] == '/' && e->name[1] != '/');

    /* Should we remove in order symlinks,objects,dirs ? */
    switch (e->type)
    {
    case CONTENTS_DIR:
      /* since the dir contains files, we remove it later */
      array_append_strcpy(dirs, e->name);
      continue;

    case CONTENTS_OBJ:
      if (protected &&
          unmerge_config_protected)
      {
        /* If the file wasn't modified, unmerge it */
        char *hash = hash_file_at(portroot_fd,
                                  e->name + 1, HASH_MD5);
        protected = 0;
        if (hash != NULL)  /* if file was not removed */
          protected = strcmp(e->digest, (const char *)hash);
      }
      break;

    case CONTENTS_SYM:
      if (fstatat(portroot_fd,
                  e->name + 1, &st, AT_SYMLINK_NOFOLLOW))
      {
        if (errno != ENOENT)
        {
          warnp("stat failed for %s -> '%s'",
                e->name, e->sym_target);
          continue;
        }
        else
        {
          break;
        }
      }

      /* Hrm, if it isn't a symlink anymore, then leave it be */
      if (!S_ISLNK(st.st_mode))
        continue;

      break;

    default:
      warn("%s???%s %s%s%s (%d)", RED, NORM,
           WHITE, e->name, NORM, e->type);
      continue;
    }

    snprintf(zing, sizeof(zing), "%s%s%s",
             protected ? YELLOW : GREEN,
             protected ? "***" : "<<<" , NORM);

    if (protected) {
      qprintf("%s %s\n", zing, e->name);
      continue;
    }

    /* See if this was updated */
    del = false;
    if (keep != NULL)
      (void)del_set(e->name, keep, &del);
    if (del)
      strcpy(zing, "---");

    /* No match, so unmerge it */
    if (!quiet)
      printf("%s %s\n", zing, e->name);
    if (!keep || !del) {
      char *p;

      if (!pretend &&
          unlinkat(portroot_fd, e->name + 1, 0))
      {
        /* If a file was already deleted, ignore the error */
        if (errno != ENOENT)
          errp("could not unlink: %s%s", portroot, e->name + 1);
      }

      p = strrchr(e->name, '/');
      if (p)
      {
        *p = '\0';
        if (!pretend)
          rmdir_r_at(portroot_fd, e->name + 1);
      }
    }
  }

  free(contentsp);

  /* Then remove all dirs in reverse order */
  array_for_each_rev(dirs, n, dir)
  {
    int rm;

    rm = pretend ? -1 : rmdir_r_at(portroot_fd, dir + 1);
    qprintf("%s%s%s %s%s%s/\n", rm ? YELLOW : GREEN, rm ? "---" : "<<<",
            NORM, DKBLUE, dir, NORM);
  }
  array_deepfree(dirs, NULL);

  if (!pretend)
  {
    buf = tree_pkg_meta(pkg_ctx, Q_EAPI);
    if (buf == NULL)
      buf = (char *)"0";  /* default */
    phases = tree_pkg_meta(pkg_ctx, Q_DEFINED_PHASES);
    if (phases != NULL)
    {
      mkdir_p(T, 0755);
      /* execute the pkg_postrm step */
      pkg_run_func_at(portroot_fd, tree_pkg_get_path(pkg_ctx),
                      phases, PKG_POSTRM,
                      T, T, buf, rpkg == NULL ? "" : rpkg->PVR);
    }

    /* remove the tmp */
    rm_rf(T);
    rmdir(T);

    /* finally delete the vdb entry */
    rm_rf_at(portroot_fd, tree_pkg_get_path(pkg_ctx));
    unlinkat(portroot_fd, tree_pkg_get_path(pkg_ctx), AT_REMOVEDIR);

    /* and prune the category if it's empty */
    snprintf(T, sizeof(T), "%s", tree_pkg_get_path(pkg_ctx));
    contentsp = strrchr(T, '/');
    if (contentsp != NULL)
      *contentsp = '\0';
    unlinkat(portroot_fd, T, AT_REMOVEDIR);
  }

  return 0;
}

static int pkg_merge
(
  tree_pkg_ctx   *mpkg,
  tree_pkg_ctx   *ipkg,
  int             cp_argc,
  char          **cp_argv,
  int             cpm_argc,
  char          **cpm_argv
)
{
  char            buf[_Q_PATH_MAX];
  struct stat     st;
  set            *objs;
  atom_ctx       *matom;
  FILE           *fp;
  FILE           *contents;
  char           *p;
  char           *D;
  char           *T;
  int             i;
  char          **iargv;
  int             iargc;
  const char     *compr;
  int             tbz2size;
  const char     *replver       = "";
  char           *eprefix       = NULL;
  size_t          eprefix_len   = 0;
  char           *pm_phases     = NULL;
  size_t          pm_phases_len = 0;
  char           *eapi          = NULL;
  size_t          eapi_len      = 0;

  if (!mpkg)
    return 1;

  matom = tree_pkg_atom(mpkg, true);

  /* create directories in the vdb repo */
  snprintf(buf, sizeof(buf), "%s/%s/%s",
           portroot, portvdb, matom->CATEGORY);
  mkdir_p(buf, 0755);

  /* set up our temp dir to unpack this stuff */
  snprintf(buf, sizeof(buf), "%s%s/qmerge/%s/%s",
           portroot, port_tmpdir, matom->CATEGORY, matom->PF);
  mkdir_p(buf, 0755);
  xchdir(buf);
  xasprintf(&D, "%s/image", buf);
  xasprintf(&T, "%s/temp", buf);

  /* ensure it is empty (rm_rf doesn't actually remove $PWD, just
   * everything under it) */
  rm_rf(".");

  if (mkdir("temp", 0755) < 0 ||
      mkdir("vdb", 0755) < 0 ||
      mkdir("image", 0755) < 0)
  {
    errf("could not create temp, vdb and image dirs!");
  }

  p = tree_pkg_get_path(mpkg);
  i = (int)strlen(p);

  /* check if the file exists, try to download it if absent */
  snprintf(buf, sizeof(buf), "%s/%s", portroot, p);
  /*FIXME: pkg_fetch() */

  if (i > sizeof(".gpkg.tar") - 1 &&
      memcmp(&p[i - (sizeof(".gpkg.tar") - 1)],
             ".gpkg.tar", sizeof(".gpkg.tar") - 1) == 0)
  { /* {{{ GPKG */
#ifdef ENABLE_GPKG
    /* unpack the whole thing to temp, dropping the pkg name dir, so
     * we end up with generic files in temp */
    struct archive       *a;
    struct archive       *t;
    struct archive_entry *entry;

    xchdir("temp");
    a = archive_read_new();
    t = archive_write_disk_new();
    archive_read_support_format_all(a);
    if (archive_read_open_filename(a, buf, BUFSIZ) != ARCHIVE_OK)
      err("failed to open %s: %s", buf, archive_error_string(a));
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
      const char *fname = archive_entry_pathname(entry);
      size_t      size;
      la_int64_t  off;

      /* drop pkg name dir prefix */
      fname = strchr(fname, '/');
      if (fname == NULL)
        continue;
      fname++;
      if (*fname == '\0')
        continue;  /* bug #968185 */

      /* drop compressor (and "tar" -- not to be misleading) for
       * easy access below */
      if (strncmp(fname, "metadata.tar", sizeof("metadata.tar") - 1) == 0)
        fname = "metadata";
      if (strncmp(fname, "image.tar", sizeof("image.tar") - 1) == 0)
        fname = "image";

      archive_entry_set_pathname(entry, fname);
      fname = archive_entry_pathname(entry);  /* re-retrieve for errors */

      if (archive_write_header(t, entry) != ARCHIVE_OK)
        err("failed to unpack from gpkg '%s': %s",
            fname, archive_error_string(t));
      while (archive_read_data_block(a, (const void **)&p,
                                     &size, &off) == ARCHIVE_OK)
      {
        if (archive_write_data_block(t, p, size, off) != ARCHIVE_OK)
          err("failed to write from gpkg '%s': %s\n",
              fname, archive_error_string(t));
      }
      archive_write_finish_entry(t);
    }
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(t);
    archive_write_free(t);
    xchdir("..");

    /* now we unpacked everything, we can extract the VDB (metadata)
     * and image */
    xchdir("vdb");
    a = archive_read_new();
    t = archive_write_disk_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    archive_write_disk_set_options(t, (ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_TIME |
                                       ARCHIVE_EXTRACT_ACL |
                                       ARCHIVE_EXTRACT_FFLAGS |
                                       ARCHIVE_EXTRACT_XATTR));
    if (archive_read_open_filename(a, "../temp/metadata",
                                   BUFSIZ) != ARCHIVE_OK)
      err("failed to open metadata: %s", archive_error_string(a));
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
      const char *fname = archive_entry_pathname(entry);
      size_t      size;
      la_int64_t  off;

      /* drop metadata prefix */
      fname = strchr(fname, '/');
      if (fname == NULL)
        continue;
      fname++;
      if (*fname == '\0')
        continue;  /* bug #968185 */

      archive_entry_set_pathname(entry, fname);
      fname = archive_entry_pathname(entry);  /* re-retrieve for errors */

      if (archive_write_header(t, entry) != ARCHIVE_OK)
        err("failed to unpack metadata '%s': %s",
            fname, archive_error_string(t));
      while (archive_read_data_block(a, (const void **)&p,
                                     &size, &off) == ARCHIVE_OK)
      {
        if (archive_write_data_block(t, p, size, off) != ARCHIVE_OK)
          err("failed to write metadata '%s': %s\n",
              fname, archive_error_string(t));
      }
      archive_write_finish_entry(t);
    }
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(t);
    archive_write_free(t);
    xchdir("..");

    /* finally the image */
    xchdir("image");
    a = archive_read_new();
    t = archive_write_disk_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    archive_write_disk_set_options(t, (ARCHIVE_EXTRACT_PERM |
                                       ARCHIVE_EXTRACT_TIME |
                                       ARCHIVE_EXTRACT_ACL |
                                       ARCHIVE_EXTRACT_FFLAGS |
                                       ARCHIVE_EXTRACT_XATTR));
    if (archive_read_open_filename(a, "../temp/image",
                                   BUFSIZ) != ARCHIVE_OK)
      err("failed to open metadata: %s", archive_error_string(a));
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
      const char *fname = archive_entry_pathname(entry);
      size_t      size;
      la_int64_t  off;

      /* drop image prefix */
      fname = strchr(fname, '/');
      if (fname == NULL)
        continue;
      fname++;
      if (*fname == '\0')
        continue;  /* bug #968185 */

      archive_entry_set_pathname(entry, fname);
      fname = archive_entry_pathname(entry);  /* re-retrieve for errors */

      /* handle hardlinks offset, #968291 */
#ifdef HAVE_ARCHIVE_ENTRY_HARDLINK_IS_SET
      if (archive_entry_hardlink_is_set(entry))
#else
      /* for Ubuntu, older libarchive has no
       * archive_entry_hardlink_is_set */
      if (archive_entry_hardlink(entry) != NULL)
#endif
      {
        const char *hlinktrg = archive_entry_hardlink(entry);
        /* drop image prefix like for the path */
        hlinktrg = strchr(hlinktrg, '/');
        if (hlinktrg == NULL ||
            hlinktrg[1] == '\0')
        {  /* really, how? */
          warn("%s has invalid hardlink target '%s', skipping",
               fname, archive_entry_hardlink(entry));
          continue;
        }
        archive_entry_set_hardlink(entry, &hlinktrg[1]);
      }

      if (archive_write_header(t, entry) != ARCHIVE_OK)
        err("failed to unpack image '%s': %s",
            fname, archive_error_string(t));
      while (archive_read_data_block(a, (const void **)&p,
                                     &size, &off) == ARCHIVE_OK)
      {
        if (archive_write_data_block(t, p, size, off) != ARCHIVE_OK)
          err("failed to write image '%s': %s\n",
              fname, archive_error_string(t));
      }
      archive_write_finish_entry(t);
    }
    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(t);
    archive_write_free(t);
    xchdir("..");
#else
    err("gpkg support not compiled in for %s", p);
#endif
  } /* }}} */
  else
  { /* {{{ xpak */
    unsigned char   iobuf[8192];
    FILE           *tarpipe;
    FILE           *tbz2f;
    size_t          n;
    size_t          rd;
    size_t          wr;
    int             vdbfd;
    int             mfd;
    int             piped = 0;
    int             err;
    file_magic_type fmt;

    tbz2size = 0;
    if ((vdbfd = open("vdb", O_RDONLY)) == -1)
      err("failed to open vdb extraction directory");
    tbz2size = xpak_extract(buf, &vdbfd, pkg_extract_xpak_cb);
    close(vdbfd);
    if (tbz2size <= 0)
      err("%s appears not to be a valid tbz2 file", p);

    /* figure out if the data is compressed differently from what the
     * name suggests, bug #660508, usage of BINPKG_COMPRESS */
    mfd = open(buf, O_RDONLY);
    fmt = file_magic_guess_fd(mfd);
    if (mfd >= 0)
      close(mfd);

    compr = "brotli -dc"; /* default: brotli; has no magic header */
    switch (fmt)
    {
    case FMAGIC_BZIP2:
      compr = "bzip2 -dc";
      break;
    case FMAGIC_GZIP:
      compr = "gzip -dc";
      break;
    case FMAGIC_XZ:
      compr = "xz -dc";
      break;
    case FMAGIC_TAR:
      compr = "";
      break;
    case FMAGIC_LZ4:
      compr = "lz4 -dc";
      break;
    case FMAGIC_ZSTD:
      /*
       * --long=31 is needed to uncompress files compressed with
       * --long=xx where xx>27. The option is "safe" in the sense
       * that not more memory is allocated than what is really
       * needed to decompress the file. See
       * https://bugs.gentoo.org/show_bug.cgi?id=634980,
       * however, on 32-bits arches this yields an parameter
       * out of bound error:
       * https://bugs.gentoo.org/show_bug.cgi?id=710444
       * https://bugs.gentoo.org/show_bug.cgi?id=754102
       * so only do this on 64-bits systems */
#if SIZEOF_SIZE_T >= 8
      compr = "zstd --long=31 -dc";
#else
      compr = "zstd -dc";
#endif
      /* If really tar -I would be used we would have to quote:
       * compr = "I \"zstd --long=31\"";
       * But actually we use a pipe (see below) */
      break;
    case FMAGIC_LZIP:
      compr = "lzip -dc";
      break;
    case FMAGIC_LZO:
      compr = "lzop -dc";
      break;
    default:
      warn("unhandled compression type, please file a bug");
      break;
    }

    /* extract the binary package data */
    /* busybox's tar has no -I option. Thus, although we possibly
     * use busybox's shell and tar, we thus pipe, expecting the
     * corresponding (de)compression tool to be in PATH; if not,
     * a failure will occur.
     * Since some tools (e.g. zstd) complain about the .bz2
     * extension, we feed the tool by input redirection. */
    snprintf(buf, sizeof(buf),
             "sh -c '%s%star -x%sf - -C image/'",
             compr, compr[0] == '\0' ? "" : " | ",
             ((verbose > 1) ? "v" : ""));

    /* start the tar pipe and copy tbz2size binpkg bytes into it
     * "manually" rather than depending on dd or head */
    if ((tarpipe = popen(buf, "w")) == NULL)
      errp("failed to start %s", buf);

    snprintf(buf, sizeof(buf), "%s/%s", portroot, p);
    if ((tbz2f = fopen(buf, "r")) == NULL)
      errp("failed to open %s for reading", p);

    for (piped = wr = 0; piped < tbz2size; piped += wr)
    {
      n = MIN(tbz2size - piped, (ssize_t)sizeof iobuf);
      rd = fread(iobuf, 1, n, tbz2f);
      if (rd == 0)
      {
        if ((err = ferror(tbz2f)) != 0)
          errp("reading %s failed", p);

        if (feof(tbz2f))
          err("unexpected EOF in %s: corrupted binpkg", p);
      }

      for (wr = n = 0; wr < rd; wr += n)
      {
        n = fwrite(iobuf + wr, 1, rd - wr, tarpipe);
        if (n != rd - wr)
        {
          if ((err = ferror(tarpipe)) != 0)
            errp("failed to unpack binpkg");

          if (feof(tarpipe))
            err("unexpected EOF trying to unpack binpkg");
        }
      }
    }

    fclose(tbz2f);

    err = pclose(tarpipe);
    if (err > 0)
      err("finishing unpack binpkg exited with status %d", err);
    else if (err < 0)
      errp("finishing unpack binpkg unsuccessful");
  } /* }}} */

  fflush(stdout);

  /* we won't realloc, so we can loose the alloc size */
  eprefix_len = eat_file("vdb/EPREFIX", &eprefix, &eprefix_len) ?
    strlen(eprefix) : 0;
  /* don't care/use the string lengths on these */
  eat_file("vdb/EAPI", &eapi, &eapi_len);
  eat_file("vdb/DEFINED_PHASES", &pm_phases, &pm_phases_len);

  /* run required phases */
  pkg_run_func("vdb", pm_phases, PKG_PRETEND, D, T, eapi, replver);
  pkg_run_func("vdb", pm_phases, PKG_SETUP,   D, T, eapi, replver);
  pkg_run_func("vdb", pm_phases, PKG_PREINST, D, T, eapi, replver);

  /* prune stuff via INSTALL_MASK */
  {
    int    imagefd = open("image", O_RDONLY);
    size_t masklen = (strlen(install_mask) + 1 +
                      15 + 1 + 14 + 1 + 14 + 1 + 1);  /* worst case scenario */
    char  *imask;
    size_t maskp;

    if (imagefd == -1)
    {
      err("Failed to open image dir");
    }
    else if (fstat(imagefd, &st) == -1)
    {
      close(imagefd);
      err("Cannot stat image dirfd");
    }
    else if (eprefix != NULL &&
             eprefix[0] == '/')
    {
      int imagepfx = openat(imagefd, eprefix + 1, O_RDONLY);
      if (imagepfx != -1)
      {
        close(imagefd);
        imagefd = imagepfx;
      }
    }

    imask = xmalloc(masklen);
    /* rely on INSTALL_MASK code to remove optional dirs */
    maskp = snprintf(imask, masklen, "%s ", install_mask);
    if (contains_set("noinfo", features))
      maskp += snprintf(imask + maskp, masklen - maskp,
                        "/usr/share/info ");
    if (contains_set("noman", features))
      maskp += snprintf(imask + maskp, masklen - maskp,
                        "/usr/share/man ");
    if (contains_set("nodoc", features))
      maskp += snprintf(imask + maskp, masklen - maskp,
                        "/usr/share/doc ");

    /* Initialize INSTALL_MASK and common stuff */
    makeargv(imask, &iargc, &iargv);
    free(imask);
    install_mask_pwd(iargc, iargv, &st, imagefd);
    freeargv(iargc, iargv);

    /* we dont care about the return code, if it's empty, we want it
     * gone */
    unlinkat(imagefd, "./usr/share", AT_REMOVEDIR);

    close(imagefd);
  }

  /* call pkg_prerm right before we merge the replacement version such
   * that any logic it defines, can use stuff installed by the package */
  if (ipkg != NULL)
  {
    replver = tree_pkg_atom(ipkg, false)->PVR;
    pkg_run_func("vdb", pm_phases, PKG_PRERM, D, T, eapi, replver);
  }

  objs = NULL;
  if ((contents = fopen("vdb/CONTENTS", "w")) == NULL)
  {
    errf("could not open vdb/CONTENTS for writing");
  }
  else
  {
    char *cpath;
    int   ret;

    cpath = xstrdup("");  /* xrealloced in merge_tree_at */

    /* TODO: use replacing to pass over pervinst->pkg for
     * VDB/CONTENTS and respect the config-protect-if-modified flag
     * like unmerge does */

    ret = merge_tree_at(AT_FDCWD, "image",
                        AT_FDCWD, portroot, contents, eprefix_len,
                        &objs, &cpath, cp_argc, cp_argv, cpm_argc, cpm_argv);

    free(cpath);

    if (ret != 0)
      errp("failed to merge to %s", portroot);

    fclose(contents);
  }

  /* unmerge any stray pieces from the older version which we didn't
   * replace */
  if (ipkg != NULL)
  {
    /* we need to really set this unmerge pending after we
     * look at contents of the new pkg */
    pkg_unmerge(ipkg, matom, objs,
                cp_argc, cp_argv, cpm_argc, cpm_argv);
  }

  /* run postinst */
  if (!pretend)
    pkg_run_func("vdb", pm_phases, PKG_POSTINST, D, T, eapi, replver);

  if (eprefix != NULL)
    free(eprefix);
  if (eapi != NULL)
    free(eapi);
  if (pm_phases != NULL)
    free(pm_phases);

  /* Clean up the package state */
  if (objs != NULL)
    free_set(objs);
  free(D);
  free(T);

  /* Update the magic counter */
  /* FIXME: check Portage's get_counter_tick_core */
  if ((fp = fopen("vdb/COUNTER", "w")) != NULL)
  {
    fputs("0", fp);
    fclose(fp);
  }

  {
    size_t len;
    /* move the local vdb copy to the final place */
    len = snprintf(buf, sizeof(buf), "%s%s/%s",
                   portroot, portvdb, matom->CATEGORY);
    mkdir_p(buf, 0755);
    snprintf(buf + len, sizeof(buf) - len, "/%s", matom->PF);
    rm_rf(buf);  /* get rid of existing dir, empty dir is fine */
    if (rename("vdb", buf) != 0)
    {
      struct stat     vst;
      int             src_fd;
      int             dst_fd;
      int             cnt;
      int             vi;
      struct dirent **files;

      /* e.g. in case of cross-device rename, try copy+delete */
      if ((src_fd = open("vdb", O_RDONLY|O_CLOEXEC|O_PATH)) < 0 ||
          fstat(src_fd, &vst) != 0 ||
          mkdir_p(buf, vst.st_mode) != 0 ||
          (dst_fd = open(buf, O_RDONLY|O_CLOEXEC|O_PATH)) < 0 ||
          (cnt = scandirat(src_fd, ".",
                           &files, filter_self_parent, NULL)) < 0)
      {
        warn("cannot stat 'vdb' or create '%s', huh?", buf);
      }
      else
      {
        /* for now we assume the VDB is a flat directory, e.g.
         * there are no subdirs */
        for (vi = 0; vi < cnt; vi++)
        {
          if (move_file(src_fd, files[vi]->d_name,
                        dst_fd, files[vi]->d_name,
                        NULL) != 0)
            warn("failed to move 'vdb/%s' to '%s': %s",
                 files[vi]->d_name, buf, strerror(errno));
        }
        scandir_free(files, cnt);
      }
    }
  }

  /* clean up our local temp dir */
  xchdir("..");
  if (!keep_work)
    rm_rf(matom->PF);
  /* don't care about return, but when empty, remove */
  rmdir("../qmerge");

  printf("%s>>>%s %s\n",
         YELLOW, NORM, atom_format("%[CAT]%[PF]", matom));

  return 0;
}

static int unlink_empty_at
(
  int         pfd,
  const char *buf
)
{
  struct stat st;
  int         fd;
  int         ret = -1;

  fd = openat(pfd, buf, O_RDONLY);
  if (fd != -1 &&
      stat(buf, &st) != -1)
  {
    if (st.st_size == 0)
      ret = unlinkat(pfd, buf, 0);
  }
  if (fd != -1)
    close(fd);
  return ret;
}

static int pkg_verify_checksums
(
  tree_pkg_ctx   *pkg,
  int             strict,
  int             display
)
{
  atom_ctx *patom = tree_pkg_atom(pkg, false);
  char     *path  = tree_pkg_get_path(pkg);
  int       ret   = 0;
  char      md5[MD5_DIGEST_LENGTH + 1];
  char      sha1[SHA1_DIGEST_LENGTH + 1];
  char     *p;
  size_t    flen;
  int       mlen;
  bool      found = false;

  if (hash_multiple_file_at(tree_pkg_get_portroot_fd(pkg), path,
                            md5, sha1, NULL, NULL, NULL,
                            &flen, HASH_MD5 | HASH_SHA1) == -1)
    errf("failed to compute hashes for %s: %s\n",
         atom_to_string(patom), strerror(errno));

  if (display)
    printf("%s:\n", atom_to_string(patom));

  p = tree_pkg_meta(pkg, Q_SIZE);
  if (p != NULL)
    mlen = atoi(p);
  else
    mlen = 0;
  if (flen != (size_t)mlen)
  {
    warn("SIZE: [%sERR%s] %zu != %s for %s from %s\n",
         RED, NORM, flen, p == NULL ? "?" : p, atom_to_string(patom), path);
    ret++;
  }
  else if (display)
  {
    printf("  SIZE: [%s;-)%s] %s\n", GREEN, NORM, p);
  }

  if ((p = tree_pkg_meta(pkg, Q_MD5)) != NULL)
  {
    if (strcmp(md5, p) == 0)
    {
      if (display)
        printf("  MD5:  [%s;-)%s] %s\n", GREEN, NORM, md5);
    }
    else
    {
      if (display)
        warn("    MD5:  [%sERR%s] (%s) != (%s) %s from %s",
             RED, NORM, md5, p, atom_to_string(patom), path);
      ret++;
    }
    found = true;
  }

  if ((p = tree_pkg_meta(pkg, Q_SHA1)) != NULL)
  {
    if (strcmp(sha1, p) == 0)
    {
      if (display)
        qprintf("  SHA1: [%s;-)%s] %s\n", GREEN, NORM, sha1);
    }
    else
    {
      if (display)
        warn("   SHA1: [%sERR%s] (%s) != (%s) %s from %s",
             RED, NORM, sha1, p, atom_to_string(patom), path);
      ret++;
    }
    found = true;
  }

  /* never return if we couldn't verify any hash */
  if (!found)
    return -1;

  if (strict &&
      ret)
    errf("strict is set in features");

  return ret;
}

static void pkg_fetch
(
  int                level,
  const depend_atom *qatom,
  tree_pkg_ctx      *mpkg
)
{
  atom_ctx *patom     = tree_pkg_atom(mpkg, false);
  int       verifyret;

  (void)level;
  (void)qatom;

  unlink_empty_at(tree_pkg_get_portroot_fd(mpkg), tree_pkg_get_path(mpkg));

  if (mkdir_p_at(tree_pkg_get_portroot_fd(mpkg), pkgdir + 1, 0755) == -1)
  {
    warn("Failed to create %s", pkgdir);
    return;
  }

  if (force_download &&
      faccessat(tree_pkg_get_portroot_fd(mpkg),
                tree_pkg_get_path(mpkg), R_OK, 0) == 0)
  {
    if (pkg_verify_checksums(mpkg, 0, 0) != 0)
      if (getenv("QMERGE") == NULL)
        unlinkat(tree_pkg_get_portroot_fd(mpkg),
                 tree_pkg_get_path(mpkg), 0);
  }

  if (faccessat(tree_pkg_get_portroot_fd(mpkg),
                tree_pkg_get_path(mpkg), R_OK, 0) != 0)
  {
    char *p;
    char  dest[_Q_PATH_MAX];

    if (verbose)
      printf("Fetching %s\n", atom_to_string(patom));

    snprintf(dest, sizeof(dest), "%s%s/%s",
             portroot, pkgdir, patom->CATEGORY);
    if (mkdir_p(dest, 0755) != 0)
    {
      warnp("Failed to create %s", dest);
      return;
    }

    /* fetch the package */
    p = tree_pkg_meta(mpkg, Q_PATH);
    if (p != NULL)
      fetch(dest, p);
    else
      warn("invalid or missing path: %s, skipping",
           p != NULL ? p : "<missing>");

    /* verify the pkg exists now. unlink if zero bytes */
    unlink_empty_at(tree_pkg_get_portroot_fd(mpkg),
                    tree_pkg_get_path(mpkg));
  }

  if (faccessat(tree_pkg_get_portroot_fd(mpkg),
                tree_pkg_get_path(mpkg), R_OK, 0) != 0)
  {
    warn("Failed to fetch %s from %s", patom->PF, binhost);
    fflush(stderr);
    return;
  }

  /* check to see if checksum matches */
  verifyret = pkg_verify_checksums(mpkg, qmerge_strict, !quiet);
  if (verifyret == -1)
  {
    warn("No checksum data for %s (try `emaint binhost --fix`)",
         tree_pkg_get_path(mpkg));
    return;
  }
  else if (verifyret == 0)
  {
    /* done? */
    return;
  }
}

static int qmerge_merge_pkgs
(
  node_t  *n,
  set_t   *parents_seen,
  int      cp_argc,
  char   **cp_argv,
  int      cpm_argc,
  char   **cpm_argv
)
{
  char                atom[_Q_PATH_MAX];
  atom_ctx           *patom    = NULL;
  node_t             *dep;
  size_t              i;
  enum tree_open_type ptype    = 0;

  array_for_each(n->predeps, i, dep)
  {
    if (qmerge_merge_pkgs(dep, parents_seen,
                          cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
  }

  if (n->pkg != NULL)
  {
    bool unseen = false;

    patom = tree_pkg_atom(n->pkg, true);
    ptype = tree_pkg_get_treetype(n->pkg);
    atom_format_r(atom, sizeof(atom),
                  "%[CAT]%[PF]%[BUILDID]%[SLOT]%[REPO]", patom);

    /* check if we already seen this one */
    set_add_unique(parents_seen, atom, &unseen);
    if (!unseen)
      return 0;
  }
  else
  {
    /* no pkg, this cannot be something we have to install */
    return 0;
  }

  if (n->type != NTYPE_UNMERGE &&
      ptype != TREETYPE_BINPKG)
  {
    warn("merging from non-binary packages not yet supported");
    return 1;
  }
  else if (n->type == NTYPE_UNMERGE &&
           ptype != TREETYPE_VDB)
  {
    warn("cannot unmerge non-installed package");
    return 1;
  }

  switch (n->type)
  {
  case NTYPE_MERGE:
    printf("%s***%s merging %s\n", GREEN, NORM, atom);
    if (pkg_merge(n->pkg, n->ipkg,
                  cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
    break;
  case NTYPE_UNMERGE:
    if (pkg_unmerge(n->pkg, NULL, NULL,
                    cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
    break;
  case NTYPE_REMERGE:
    printf("%s***%s remerging %s\n", YELLOW, NORM, atom);
    if (pkg_merge(n->pkg, n->ipkg,
                  cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
    break;
  case NTYPE_UPGRADE:
    printf("%s***%s upgrading %s [%s]\n",
           GREEN, NORM, atom, tree_pkg_atom(n->ipkg, false)->PVR);
    if (pkg_merge(n->pkg, n->ipkg,
                  cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
    break;
  case NTYPE_DOWNGRADE:
    printf("%s***%s downgrading %s [%s]\n",
           YELLOW, NORM, atom, tree_pkg_atom(n->ipkg, false)->PVR);
    if (pkg_merge(n->pkg, n->ipkg,
                  cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
    break;
  default:
    break;  /* ignore this node */
  }

  /* post deps depend on us */
  array_for_each(n->postdeps, i, dep)
  {
    if (qmerge_merge_pkgs(dep, parents_seen,
                          cp_argc, cp_argv, cpm_argc, cpm_argv) != 0)
      return 1;
  }

  return 0;
}

typedef struct resolve_state_ {
  tree_ctx *tree;
  hash_t   *nodes;
  hash_t   *blockers;
  bool      unmerge:1;
  bool      upgrade:1;      /* only upgrade versions for explicit arguments */
  bool      upgrade_all:1;  /* deep upgrade, also for dependencies */
  bool      interactive:1;
} resolve_state_t;

static array *qmerge_read_file
(
  const char *path,
  const char *file
)
{
  char   pth[_Q_PATH_MAX];
  array *ret;
  char  *buf     = NULL;
  char  *nexttok;
  char  *line;
  char  *p;
  size_t len     = 0;

  snprintf(pth, sizeof(pth), "%s%s%s/%s", portroot, configroot, path, file);
  if (!eat_file(pth, &buf, &len))
  {
    warnp("unable to read file /%s%s/%s", configroot, path, file);
    return NULL;
  }

  ret = array_new();

  for (line = strtok_r(buf, "\n", &nexttok);
       line != NULL;
       line = strtok_r(NULL, "\n", &nexttok))
  {
    /* drop comments */
    if ((p = strchr(line, '#')) != NULL)
      *p = '\0';
    rmspace(line);
    if (line[0] == '\0')
      continue;
    array_append_strcpy(ret, line);
  }

  free(buf);

  return ret;
}

static void *qmerge_add_set_system(void *data, char *buf)
{
  set *q = data;
  char *s;

  s = strchr(buf, '#');
  if (s)
    *s = '\0';
  rmspace(buf);

  s = buf;
  if (*s == '*')
  {
    q = add_set(s + 1, q);
  }
  else if (s[0] == '-' && s[1] == '*')
  {
    bool ok;
    (void)del_set(s + 2, q, &ok);
  }

  return q;
}

static node_t *qmerge_new_node
(
  node_type_t  type,
  node_t      *parent
)
{
  node_t *ret = xzalloc(sizeof(*ret));

  ret->type     = type;
  ret->parents  = array_new();
  ret->postdeps = array_new();
  ret->predeps  = array_new();

  if (parent != NULL)
    array_append(ret->parents, parent);

  return ret;
}

static void qmerge_free_node
(
  node_t *n
)
{
  array_free(n->parents);
  array_free(n->predeps);
  array_free(n->postdeps);
  dep_free(n->dep);
  array_free(n->pkgs);
  free(n->mask);
  free(n);
}

static bool qmerge_print_node_d
(
  node_t  *n,
  set_t   *parents_seen,
  int      level
)
{
  char                atom[_Q_PATH_MAX];
  atom_ctx           *patom    = NULL;
  atom_ctx           *iatom    = NULL;
  node_t             *dep;
  const char         *tc;
  size_t              i;
  enum tree_open_type ptype    = 0;
  char                t;
  bool                repodiff = false;
  bool                nl       = true;
  bool                stop     = false;

  array_for_each(n->predeps, i, dep)
  {
    if (!qmerge_print_node_d(dep, parents_seen, level))
      return false;
  }

  /* we are a dependency of the pre-deps */
  if (array_cnt(n->predeps) > 0)
    level++;

  if (n->pkg != NULL)
  {
    bool unseen = false;

    patom = tree_pkg_atom(n->pkg, true);
    ptype = tree_pkg_get_treetype(n->pkg);
    atom_format_r(atom, sizeof(atom),
                  "%[CAT]%[PF]%[BUILDID]%[SLOT]%[REPO]", patom);

    /* check if we already printed this one */
    set_add_unique(parents_seen, atom, &unseen);
    if (!unseen)
      return true;
  }
  else
  {
    atom[0] = '\0';
  }

  if (n->ipkg != NULL)
    iatom = tree_pkg_atom(n->ipkg, true);

  if (patom != NULL &&
      iatom != NULL &&
      iatom->REPO != NULL)
  {
    if (patom->REPO == NULL)
      repodiff = true;
    else if (iatom->REPO != patom->REPO)
      repodiff = strcmp(iatom->REPO, patom->REPO) != 0;
  }

  switch (ptype)
  {
  case TREETYPE_BINPKG:   tc = MAGENTA; t = 'B';   break;
  case TREETYPE_EBUILD:   tc = BLUE;    t = 'E';   break;
  case TREETYPE_GTREE:    tc = BLUE;    t = 'E';   break;
  case TREETYPE_VDB:      tc = DKGREEN; t = 'i';   break;
  default:                tc = RED;     t = '?';   break;
  }

  /* the tree columns indicate state: [123]
   * 1: single action,                N(ew) D(elete)
   * 2: replacement on filesystem,    U(pgrade) R(einstall) d(owngrade) S(lot)
   * 3: where the package comes from: i(nstalled) E(build) B(inpkg)
   */
  switch (n->type)
  {
  case NTYPE_MERGE:
    printf("[%sN%s %s%c%s]%*s %s", GREEN, NORM, tc, t, NORM, level, "", atom);
    break;
  case NTYPE_UNMERGE:
    printf("[%sD%s %s%c%s]%*s %s", RED, NORM, tc, t, NORM, level, "", atom);
    break;
  case NTYPE_REMERGE:
    printf("[ %sR%s%s%c%s]%*s %s", YELLOW, NORM, tc, t, NORM, level, "", atom);
    if (repodiff)
      printf(" (%s)", atom_format("%[REPO]", iatom));
    break;
  case NTYPE_UPGRADE:
    printf("[ %sU%s%s%c%s]%*s %s", GREEN, NORM, tc, t, NORM, level, "", atom);
    if (repodiff)
      printf(" (%s)", atom_format("%[PVR]%[SLOT]%[REPO]", iatom));
    else
      printf(" (%s)", atom_format("%[PVR]%[SLOT]", iatom));
    break;
  case NTYPE_DOWNGRADE:
    printf("[ %sd%s%s%c%s]%*s %s", YELLOW, NORM, tc, t, NORM, level, "", atom);
    if (repodiff)
      printf(" (%s)", atom_format("%[PVR]%[SLOT]%[REPO]", iatom));
    else
      printf(" (%s)", atom_format("%[PVR]%[SLOT]", iatom));
    break;
  case NTYPE_INSTALLED:
    if (!nocolor)
      color_clear();
    atom_format_r(atom, sizeof(atom),
                  "%[CAT]%[PF]%[BUILDID]%[SLOT]%[REPO]", patom);
    if (!nocolor)
      color_remap();
    printf("%s[  %c]%*s %s%s", BRYELLOW, t, level, "", atom, NORM);
    break;
  case NTYPE_CONFLICT:
    printf("[%s!%s %s%c%s]%*s %s\n", RED, NORM, tc, t, NORM, level, "", atom);
    atom_format_r(atom, sizeof(atom), "%[CAT]%[PF]%[SLOT]%[REPO]", iatom);
    switch (tree_pkg_get_treetype(n->ipkg))
    {
    case TREETYPE_BINPKG:   tc = MAGENTA; t = 'B';   break;
    case TREETYPE_EBUILD:   tc = BLUE;    t = 'E';   break;
    case TREETYPE_GTREE:    tc = BLUE;    t = 'E';   break;
    case TREETYPE_VDB:      tc = DKGREEN; t = 'I';   break;
    default:                tc = RED;     t = '?';   break;
    }
    printf(" %s!%s %s%c%s %*s %s\n", RED, NORM, tc, t, NORM, level, "", atom);
    printf("      %sthe two packages above cannot be installed "
           "at the same time%s", RED, NORM);

    stop = true;
    break;
  case NTYPE_AMBIGIOUS:
    {
      tree_pkg_ctx *pkg;

      array_for_each(n->pkgs, i, pkg)
      {
        if (!nocolor)
          color_clear();
        atom_format_r(atom, sizeof(atom), "%[CAT]%[PF]%[SLOT]%[REPO]",
                      tree_pkg_atom(pkg, true));
        if (!nocolor)
          color_remap();
        switch (tree_pkg_get_treetype(pkg))
        {
        case TREETYPE_BINPKG:   tc = MAGENTA; t = 'B';   break;
        case TREETYPE_EBUILD:   tc = BLUE;    t = 'E';   break;
        case TREETYPE_GTREE:    tc = BLUE;    t = 'E';   break;
        case TREETYPE_VDB:      tc = DKGREEN; t = 'I';   break;
        }
        printf("[%s?%s %s%c%s] (%s%2zu%s)%*s %s%s%s\n",
               YELLOW, NORM, tc, t, NORM,
               GREEN, i + 1, NORM, level, "",
               DKBLUE, atom, NORM);
      }
      printf("      %sthe %s%zu%s packages above match the "
             "argument %s\"%s\"%s",
             RED, MAGENTA, i, RED, DKBLUE, atom_to_string(n->atom), NORM);

      stop = true;
    }
    break;
  case NTYPE_MASKED:
  case NTYPE_ERROR:
    if (n->pkg != NULL)
    {
      atom_format_r(atom, sizeof(atom), "%[CAT]%[PF]%[SLOT]%[REPO]",
                    tree_pkg_atom(n->pkg, true));
    }
    else if (n->atom != NULL)
    {
      atom_to_string_r(atom, sizeof(atom), n->atom);
    }
    else
    {
      snprintf(atom, sizeof(atom), "%sreason missing from error%s",
               MAGENTA, NORM);
    }
    printf("[%s! !%s]%*s %s\n", RED, NORM, level, "", atom);
    if (n->dep)
    {
      atom_ctx *failatom  = dep_node_fail_input(n->dep);
      array    *failatoms = array_new();

      if (failatom != NULL)
        array_append(failatoms, failatom);

      printf("      %sfailed to resolve dependency:%s\n", RED, NORM);
      dep_print(stdout, n->dep, 2, failatoms, RED, 0);
      if (failatom != NULL)
        atom_to_string_r(atom, sizeof(atom), failatom);
      array_free(failatoms);
    }
    if (n->type == NTYPE_MASKED)
      printf("      %scould not find a package matching %s%s%s, "
             "%s is masked by %s",
             RED, DKBLUE, atom_to_string(n->atom), NORM, atom, n->mask);
    else
      printf("      %scould not find a package matching %s%s%s",
             RED, DKBLUE, atom, NORM);

    stop = true;
    break;
  case NTYPE_ROOT:
  case NTYPE_IGNORE:
    /* this node has no representation */
    nl = false;
    break;
  }

  if (n->is_arg &&
      n->type != NTYPE_AMBIGIOUS &&
      n->atom != NULL)
    printf(" <- %sarg%s \"%s\"\n",
           DKBLUE, NORM, atom_to_string(n->atom));
  else if (nl)
    printf("\n");

  if (stop)
    return false;

  /* post deps depend on us */
  array_for_each(n->postdeps, i, dep)
  {
    if (!qmerge_print_node_d(dep, parents_seen, level + 1))
      return false;
  }

  return true;
}

static void qmerge_print_node
(
  node_t  *n
)
{
  set_t *parents_seen = set_new();

  qmerge_print_node_d(n, parents_seen, 0);

  set_free(parents_seen);
}

static dep_status_t qmerge_resolve_dep
(
  node_t          *node,
  resolve_state_t *state,
  bool             explicit
)
{
  node_t       *dep;
  atom_ctx     *atom;
  array        *match;
  char         *str;
  size_t        n;
  dep_status_t  dres;

  if (node->pkg != NULL &&
      node->ipkg != NULL)
  {
    if (!explicit &&
        (!state->upgrade_all ||
         node->pkg == node->ipkg))
    {
      node->type = NTYPE_INSTALLED;
    }
    else
    {
      switch (atom_compare(tree_pkg_atom(node->ipkg, true),
                           tree_pkg_atom(node->pkg, true)))
      {
      case NOT_EQUAL: /* maybe SLOT difference? */
        node->type = NTYPE_MERGE;
        break;
      case EQUAL:
        if (explicit &&
            !state->upgrade)
          node->type = NTYPE_REMERGE;
        else
          node->type = NTYPE_INSTALLED;
        break;
      case NEWER:
        node->type = NTYPE_DOWNGRADE;
        break;
      case OLDER:
        if (explicit ||
            state->upgrade_all)
          node->type = NTYPE_UPGRADE;
        else
          node->type = NTYPE_INSTALLED;
        break;
      default:
        /* error; how? just merge it */
        node->type = NTYPE_MERGE;
        break;
      }
    }
  }
  else if (node->pkg != NULL)
  {
    node->type = NTYPE_MERGE;
  }
  else
  {
    /* both pkg and ipkg are NULL, this atom is a blocker, then there's
     * nothing to do, it was added to the blockers list before */
    if (node->atom != NULL &&
        node->atom->blocker != ATOM_BL_NONE)
    {
      node->type = NTYPE_IGNORE;
      return DEP_OK;
    }

    /* FIXME: how can we set found bit here, and do we really need it? */
    return DEP_FAIL;
  }

  atom = tree_pkg_atom(node->pkg, true);
  str  = atom_format("%[CAT]%[PN]%[SLOT]", atom);
  dep  = hash_get(state->nodes, str);
  if (dep != NULL)
  {
    if (atom_compare(tree_pkg_atom(dep->pkg, true), atom) != EQUAL)
    {
      /* conflicting deps */
      node_t *conflict = qmerge_new_node(NTYPE_CONFLICT, node);
      conflict->pkg  = node->pkg;
      conflict->ipkg = dep->pkg;
      array_append(node->predeps, conflict);
      return DEP_FAIL;
    }

    /* node already present, don't re-resolve it */
    return DEP_OK;
  }
  else
  {
    hash_add(state->nodes, str, node, NULL);
  }

  /* up to here, all seems well */
  dres = DEP_OK;

  if (node->type != NTYPE_INSTALLED)
  {
    struct {
      enum tree_pkg_meta_keys key;
      array                  *store;
      bool                    binuse;
    } depends[] = {
      { Q_DEPEND,   node->predeps,  false },
      { Q_RDEPEND,  node->postdeps, true  },
      { Q_PDEPEND,  node->postdeps, true  },
      { Q_BDEPEND,  node->predeps,  false },
      { Q_IDEPEND,  node->postdeps, true  },
      { Q_UNKNOWN,  NULL,           false }
    };
    dep_node_t *deps;
    dep_node_t *depw;
    char       *p;
    int         i;
    bool        binpkg = tree_pkg_get_treetype(node->pkg) == TREETYPE_BINPKG;

    /* get dependencies and add them by recursing */
    for (i = 0; depends[i].key != Q_UNKNOWN; i++)
    {
      if (binpkg &&
          !depends[i].binuse)
        continue;

      p = tree_pkg_meta(node->pkg, depends[i].key);
      if (p == NULL)
        continue;

      deps = dep_new(p);
      dres = dep_resolve_tree(deps, state->tree, ev_use,
                              state->blockers, accept_keywords);

      if (dres == DEP_FAIL)
      {
        /* TODO: need to get at what dep failed for */
        node_t *err = qmerge_new_node(NTYPE_ERROR, node);
        err->pkg = node->pkg;
        err->dep = deps;
        deps     = NULL;
        array_append(depends[i].store, err);
      }
      else if (dres == DEP_MASK)
      {
        node_t *err = qmerge_new_node(NTYPE_MASKED, node);
        err->pkg  = node->pkg;
        err->atom = dep_node_atom(deps);
        err->mask = xstrdup(atom_to_string(dep_node_mask(deps)));
        err->dep  = deps;
        deps      = NULL;
        array_append(depends[i].store, err);
      }
      else if (dres == DEP_KEYWORD)
      {
        node_t *err = qmerge_new_node(NTYPE_MASKED, node);
        err->pkg  = node->pkg;
        err->atom = dep_node_atom(deps);
        err->mask = xstrdup("missing keyword");
        err->dep  = deps;
        deps      = NULL;
        array_append(depends[i].store, err);
      }
      else if (dres == DEP_OK)
      {
        match = dep_nodes(deps);
        array_for_each(match, n, depw)
        {
          dep = qmerge_new_node(NTYPE_ERROR, node);  /* temp type */
          dep->pkg  = dep_node_pkg(depw);
          dep->ipkg = dep_node_ipkg(depw);
          dep->atom = dep_node_atom(depw);
          array_append(depends[i].store, dep);

          if (dep->atom != NULL)
            dep->atom = atom_clone(dep->atom);

          dres = qmerge_resolve_dep(dep, state, false);
        }
        array_free(match);
      }

      dep_free(deps);

      if (dres != DEP_OK)
        break;
    }
  }

  return dres;
}

static dep_status_t qmerge_resolve
(
  char            *thing,
  resolve_state_t *state,
  node_t          *root
)
{
  array        *alist = NULL;
  atom_ctx     *atom;
  tree_pkg_ctx *pkg;
  node_t       *node;
  dep_status_t  ret   = DEP_FAIL;
  size_t        n;
  bool          isset = false;

  if (thing[0] == '@')
  {
    isset = true;
    thing++;
  }

  if (strcmp(thing, "world") == 0)
  {
    if (root->type != NTYPE_ROOT)
      return DEP_FAIL;

    alist = qmerge_read_file("/var/lib/portage", "world");
    if (alist == NULL)
      return DEP_FAIL;
    isset = false;
  }
  else if (strcmp(thing, "system") == 0)
  {
    set_t *q = set_new();
    array *a;
    char  *p;

    if (root->type != NTYPE_ROOT)
      return DEP_FAIL;

    q = q_profile_walk("packages", qmerge_add_set_system, q);

    alist = array_new();
    a     = set_keys(q);
    array_for_each(a, n, p)
    {
      array_append_strcpy(alist, p);
    }
    array_free(a);
    set_free(q);
    isset = false;
  }
  else if (strcmp(thing, "all") == 0)
  {
    tree_ctx     *ctx;
    array        *pkgs;
    tree_pkg_ctx *w;
    const char   *fmt;
    char         *a;

    if (root->type != NTYPE_ROOT)
      return DEP_FAIL;

    ctx  = tree_new(portroot, portvdb, TREETYPE_VDB, true);
    pkgs = tree_match_atom(ctx, NULL, TREE_MATCH_DEFAULT);

    /* maybe: if we want to re-install the exact same versions:
      fmt = "%[CAT]%[PF]";
     */
    fmt = "%[CAT]%[PN]%[SLOT]";

    alist = array_new();

    array_for_each(pkgs, n, w)
    {
      a = atom_format(fmt, tree_pkg_atom(w, true));
      array_append_strcpy(alist, a);
    }

    array_free(pkgs);
    tree_close(ctx);

    isset = false;
  }

  if (isset)
  {
    if (root->type != NTYPE_ROOT)
      return DEP_FAIL;

    alist = qmerge_read_file("/etc/portage/sets", thing);
    if (alist == NULL)
      return DEP_FAIL;
  }

  if (alist != NULL)
  {
    char *patom;
    array_for_each(alist, n, patom)
    {
      if ((ret = qmerge_resolve(patom, state, root)) != DEP_OK)
        break;
    }
    array_deepfree(alist, NULL);

    return ret;
  }

  rmspace(thing);

  atom  = atom_explode(thing);
  isset = false;

  if (state->unmerge)
  {
    /* find *all* (installed) packages */
    /* we don't have access to the VDB tree separately, but since
     * unmerging isn't very safe at this point (because we don't check the
     * revdeps) let's leave this for another day */
    alist = tree_match_atom(state->tree, atom, TREE_MATCH_DEFAULT);
    array_for_each(alist, n, pkg)
    {
      if (tree_pkg_get_treetype(pkg) == TREETYPE_VDB)
      {
        node         = qmerge_new_node(NTYPE_UNMERGE, root);
        node->atom   = atom;
        node->pkg    = pkg;
        node->ipkg   = pkg;
        node->is_arg = true;
        array_append(root->predeps, node);

        /* TODO: see above, pick up deps that would need us
        ret       = qmerge_resolve_dep(node, state, true);
        */
        ret       = DEP_OK;
        isset     = true;
      }
    }
  }
  else
  {
    tree_pkg_ctx *ipkg     = NULL;
    array        *ambs     = array_new();
    tree_pkg_ctx *nextpkg;
    char         *lastcat  = NULL;
    char         *curcat;
    size_t        found;
    bool          ismasked = false;
    bool          unkeywd  = false;

    /* would love to use dep_resolve_tree here, but the essential
     * difference/problem is that we have possibly ambigious/incomplete
     * input, e.g. an atom like "bc" -- in the tree fashion this can
     * never happen, but as user input, yes it can, so we manually need
     * to resolve it down to qualifying atoms */

    /* find best version of any package that matches (in any tree) */
    alist = tree_match_atom(state->tree, atom, (TREE_MATCH_DEFAULT |
                                                TREE_MATCH_SORT));
    array_for_each(alist, n, pkg)
    {
      set_t *keywords;
      char  *kwstr;
      bool   haskwd   = true;

      if (tree_pkg_get_treetype(pkg) == TREETYPE_VDB)
      {
        /* duplicates here don't matter, we're trying to install
         * something here */
        ipkg = pkg;
        continue;
      }

      /* binpkgs and ebuild trees can all provide the same pkgs, the
       * order in which they are returned is not defined, so for
       * exact duplicates we have to select the binary package manually */
      nextpkg = array_get(alist, n + 1);
      if (nextpkg != NULL &&
          atom_compare(tree_pkg_atom(pkg, false),
                       tree_pkg_atom(nextpkg, false)) == EQUAL)
      {
        if (tree_pkg_get_treetype(pkg) == TREETYPE_EBUILD &&
            tree_pkg_get_treetype(nextpkg) != TREETYPE_VDB)
          pkg = nextpkg;  /* must be non-ebuild */

        /* skip next package, we already evaluated it */
        n++;
      }

      /* check state->blockers */
      if (state->blockers != NULL)
      {
        array    *blkatoms;
        atom_ctx *blkatom;
        size_t    m;

        blkatoms = hash_get(state->blockers,
                            atom_format("%[CAT]%[PN]%[SLOT]",
                                        tree_pkg_atom(pkg, true)));
        array_for_each(blkatoms, m, blkatom)
        {
          /* we should only have masks at this point */
          /*
          warn("blocker: %s", atom_to_string(blkatom));
          warn("pkg:     %s", atom_to_string(tree_pkg_atom(pkg, true)));
          */
          if (atom_compare(tree_pkg_atom(pkg, true), blkatom) == EQUAL)
          {
            ismasked = true;
            break;
          }
        }

        if (ismasked)
          continue;
      }

      /* check accept_keywords */
      kwstr = tree_pkg_meta(pkg, Q_KEYWORDS);
      if (kwstr == NULL)
      {
        haskwd = false;
      }
      else
      {
        keywords = set_add_from_string(set_new(), kwstr);
        if (!set_has_intersection(keywords, accept_keywords))
          haskwd = false;
        set_free(keywords);
      }

      if (!haskwd)
      {
        unkeywd = true;
        continue;
      }

      /* for us to consider something ambigious, only the category must
       * be different */
      curcat = tree_pkg_atom(pkg, false)->CATEGORY;
      if (lastcat == NULL ||
          strcmp(curcat, lastcat) != 0)
        array_append(ambs, pkg);
      lastcat = curcat;
    }

    found = array_cnt(ambs);

    if (found > 1)
    {
      /* create duplicate error node */
      node         = qmerge_new_node(NTYPE_AMBIGIOUS, root);
      node->atom   = atom;
      node->pkgs   = ambs;
      node->is_arg = true;
      array_append(root->predeps, node);

      ambs = NULL;  /* now in the node */
    }
    else if (found == 1)
    {
      /* take best (== first) match */
      pkg = array_get(ambs, 0);

      node         = qmerge_new_node(NTYPE_ERROR, root);  /* temp type */
      node->atom   = atom;
      node->pkg    = pkg;
      node->ipkg   = ipkg;
      node->is_arg = true;
      array_append(root->predeps, node);

      ret   = qmerge_resolve_dep(node, state, true);
      isset = true;
    }
    else
    {
      if (ismasked)
      {
        node       = qmerge_new_node(NTYPE_MASKED, root);
        node->pkg  = pkg;
        node->mask = xstrdup("package.mask");  /*FIXME*/
      }
      else if (unkeywd)
      {
        node       = qmerge_new_node(NTYPE_MASKED, root);
        node->pkg  = pkg;
        node->mask = xstrdup("missing keyword");
      }
      else
      {
        /* no match found */
        node       = qmerge_new_node(NTYPE_ERROR, root);
      }
      node->atom   = atom;
      node->is_arg = true;
      array_append(root->predeps, node);
    }

    array_free(ambs);
  }

  array_free(alist);

  if (!isset)
    ret = DEP_FAIL;

  return ret;
}

int qmerge_main
(
  int    argc,
  char **argv
)
{
  array          *args  = array_new();
  tree_ctx       *tree;
  tree_ctx       *vdb;
  node_t         *root  = NULL;
  resolve_state_t rstate;
  dep_status_t    res_state;
  int             i;
  int             ret   = EXIT_SUCCESS;
  bool            abort;
  bool            binpkgonly = false;

  if (argc < 2)
    qmerge_usage(EXIT_FAILURE);

  VAL_CLEAR(rstate);

  tree = vdb = tree_new(portroot, portvdb, TREETYPE_VDB, false);
  if (tree == NULL)
    err("cannot function without VDB");
  rstate.tree     = tree;
  rstate.blockers = hash_new();

  while ((i = GETOPT_LONG(QMERGE, qmerge, "")) != -1)
  {
    switch (i)
    {
    case 'f': force_download = 1;  break;
    case 'F': force_download = 2;  break;
    case 'K': binpkgonly = true;   break;
    case 'U': uninstall = 1;       break;
    case 'p': pretend = 1;         break;
    case 'u': update_only = 1;     break;
    case 'y': interactive = 0;     break;
    case 'O': follow_rdepends = 0; break;
    case 127: keep_work = true;    break;
    case 128: debug = true;        break;
    /* in case tree has https support, something like this could/should
     * work:
    case 'P': tree = tree_new(portroot, argv[optind], TREETYPE_BINPKG, false);
              rstate.tree = tree_merge(rstate.tree, tree);
     */
              COMMON_GETOPTS_CASES(qmerge)
    }
  }

  qmerge_strict = contains_set("strict", features) ? 1 : 0;

  if (uninstall != 0)
    rstate.unmerge = true;
  if (update_only != 0)
    rstate.upgrade = true;
  if (interactive != 0)
    rstate.interactive = true;

  /* add local binpkgs when present */
  if (!uninstall)
  {
    tree = tree_new(portroot, pkgdir, TREETYPE_BINPKG, true);
    if (tree != NULL)
      rstate.tree = tree_merge(rstate.tree, tree);
  }

  /* add ebuild trees */
  if (!uninstall &&
      !binpkgonly)
  {
    char  *overlay;
    size_t n;

    array_for_each(overlays, n, overlay)
    {
      tree = tree_new(portroot, overlay, TREETYPE_EBUILD, true);
      if (tree != NULL)
        rstate.tree = tree_merge(rstate.tree, tree);
    }
  }

  /* create a list we can modify if we have to resolve things */
  for (i = optind; i < argc; i++)
    array_append_strcpy(args, argv[i]);

  /* convert masks into a form that can be used */
  if (!uninstall)
  {
    array    *masks = hash_keys(package_masks);
    array    *atoms;
    char     *mask;
    atom_ctx *atom;
    size_t   n;

    array_for_each(masks, n, mask)
    {
      atom = atom_explode(mask);
      if (atom != NULL)
      {
        const char *hashkey = atom_format("%[CAT]%[PN]%[SLOT]", atom);
        atoms = hash_get(rstate.blockers, hashkey);
        if (atoms == NULL)
        {
          atoms = array_new();
          hash_add(rstate.blockers, hashkey, atoms, NULL /* must be NULL */);
        }

        /* we're assuming all masks here are unique, if they aren't it's
         * no big deal, it just costs us more to evaluate */
        array_append(atoms, atom);
      }
    }

    array_free(masks);
  }

  /* resolve the input */
  abort = false;
  do
  {
    char   *arg;
    node_t *node;
    size_t  n;
    size_t  m;

    if (root != NULL)
      qmerge_free_node(root);

    /* load atoms and expand any sets given on the command line */
    rstate.nodes = hash_new();
    root         = qmerge_new_node(NTYPE_ROOT, NULL);

    array_for_each(args, n, arg)
    {
      if ((res_state = qmerge_resolve(arg, &rstate, root)) != DEP_OK)
        break;
    }
    hash_free(rstate.nodes);

    array_for_each(root->predeps, m, node)
    {
      if (node->type == NTYPE_AMBIGIOUS)
      {
        char buf[32];
        int  num = 0;

        qmerge_print_node(node);
        if (!interactive)
        {
          abort = true;
          break;  /* just report the problem, then stop */
        }

        printf("\n"
               "%sPlease specify which package to select%s [%s0%s,%s1-%zu%s] ",
               BOLD, NORM, RED, NORM, GREEN, array_cnt(node->pkgs), NORM);
        fflush(stdout);
        buf[0] = '\0';
        if (fgets(buf, sizeof(buf), stdin) != NULL &&
            (num = atoi(buf)) > 0 &&
            num <= array_cnt(node->pkgs))
        {
          tree_pkg_ctx *pkg  = array_get(node->pkgs, num - 1);
          char         *atom = atom_to_string(tree_pkg_atom(pkg, true));

          array_delete(args, n, NULL);
          array_append_strcpy(args, atom);

          res_state = DEP_NEWBLOCKER;  /* try again with this replacement */
          printf("\n");
          break;
        }
        else
        {
          rmspace(buf);
          if (num == 0)
            printf("Aborting on user input\n");
          else
            printf("%sAbort: invalid user input%s '%s'\n", RED, NORM, buf);
          abort = true;
        }
      }
    }
  }
  while (res_state == DEP_NEWBLOCKER);

  /* show the result */
  if (!abort)
  {
    printf("These are the packages involved in this operation:\n");
    qmerge_print_node(root);
  }

  if (res_state != DEP_OK)
  {
    ret = EXIT_FAILURE;
  }
  else
  {
    if (interactive)
    {
      printf("\n");
      if (uninstall)
      {
        if (!qmerge_prompt("OK to unmerge these packages"))
          ret = EXIT_FAILURE;
      }
      else
      {
        if (!qmerge_prompt("OK to merge these packages"))
          ret = EXIT_FAILURE;
      }
    }

    if (ret != EXIT_FAILURE)
    {
      set_t *parents_seen = set_new();
      char **cp_argv;
      char **cpm_argv;
      int    cp_argc;
      int    cpm_argc;

      makeargv(config_protect, &cp_argc, &cp_argv);
      makeargv(config_protect_mask, &cpm_argc, &cpm_argv);

      ret = qmerge_merge_pkgs(root, parents_seen,
                              cp_argc, cp_argv,
                              cpm_argc, cpm_argv);
      if (ret == 1)
        ret = EXIT_FAILURE;

      freeargv(cp_argc, cp_argv);
      freeargv(cpm_argc, cpm_argv);
      set_free(parents_seen);
    }
  }

  qmerge_free_node(root);
  tree_close(rstate.tree);

  return ret;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
