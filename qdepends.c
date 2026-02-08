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

#include <ctype.h>
#include <xalloc.h>
#include <assert.h>

#include "array.h"
#include "atom.h"
#include "dep.h"
#include "set.h"
#include "tree.h"
#include "xasprintf.h"
#include "xregex.h"

#define QDEPENDS_FLAGS "drpbIQitUF:SR" COMMON_FLAGS
static struct option const qdepends_long_opts[] = {
  {"depend",    no_argument, NULL, 'd'},
  {"rdepend",   no_argument, NULL, 'r'},
  {"pdepend",   no_argument, NULL, 'p'},
  {"bdepend",   no_argument, NULL, 'b'},
  {"idepend",   no_argument, NULL, 'I'},
  {"query",     no_argument, NULL, 'Q'},
  {"installed", no_argument, NULL, 'i'},
  {"tree",      no_argument, NULL, 't'},
  {"use",       no_argument, NULL, 'U'},
  {"format",     a_argument, NULL, 'F'},
  {"pretty",    no_argument, NULL, 'S'},
  {"resolve",   no_argument, NULL, 'R'},
  COMMON_LONG_OPTS
};
static const char * const qdepends_opts_help[] = {
  "Show DEPEND info",
  "Show RDEPEND info",
  "Show PDEPEND info",
  "Show BDEPEND info",
  "Show IDEPEND info",
  "Query reverse deps",
  "Search installed packages using VDB",
  "Search available ebuilds in the tree",
  "Apply profile USE-flags to conditional deps",
  "Print matched atom using given format string",
  "Pretty format specified depend strings",
  "Resolve found dependencies to package versions",
  COMMON_OPTS_HELP
};
#define qdepends_usage(ret) usage(ret, QDEPENDS_FLAGS, qdepends_long_opts, qdepends_opts_help, NULL, lookup_applet_idx("qdepends"))

/* structures / types / etc ... */
struct qdepends_opt_state {
  unsigned int  qmode;
  array        *atoms;
  set          *udeps;
  char         *depend;
  size_t        depend_len;
  const char   *format;
  char          resolve:1;
  tree_ctx     *vdb;
};

#define QMODE_DEPEND     (1<<0)
#define QMODE_RDEPEND    (1<<1)
#define QMODE_PDEPEND    (1<<2)
#define QMODE_BDEPEND    (1<<3)
#define QMODE_IDEPEND    (1<<4)
#define QMODE_INSTALLED  (1<<5)
#define QMODE_TREE       (1<<6)
#define QMODE_REVERSE    (1<<7)
#define QMODE_FILTERUSE  (1<<8)

#define QMODE_DEP_FIRST  QMODE_DEPEND
#define QMODE_DEP_LAST   QMODE_IDEPEND

const char *depend_files[] = {  /* keep *DEPEND aligned with above defines */
  /* 0 */ "DEPEND",
  /* 1 */ "RDEPEND",
  /* 2 */ "PDEPEND",
  /* 3 */ "BDEPEND",
  /* 4 */ "IDEPEND",
  /* 5 */ NULL
};

static bool qdepends_print_depend
(
  FILE       *fp,
  const char *depend
)
{
  dep_node_t *dep_tree;

  dep_tree = dep_grow_tree(depend);
  if (dep_tree == NULL)
    return false;

  if (!quiet)
    fprintf(fp, "DEPEND=\"\n");

  dep_print_tree(fp, dep_tree, 1, NULL, NORM, verbose > 1);

  dep_burn_tree(dep_tree);
  if (!quiet)
    fprintf(fp, "\"\n");

  return true;
}

static int
qdepends_results_cb
(
  tree_pkg_ctx *pkg_ctx,
  void         *priv
)
{
  char                       buf[_Q_PATH_MAX];
  struct qdepends_opt_state *state      = priv;
  atom_ctx                  *atom;
  atom_ctx                  *datom;
  atom_ctx                  *fatom;
  array                     *deps;
  dep_node_t                *dep_tree;
  char                      *depstr;
  const char               **dfile;
  size_t                     i;
  size_t                     n;
  size_t                     m;
  int                        ret        = 0;
  bool                       firstmatch = false;

  /* matrix consists of:
   * - QMODE_*DEPEND
   * - QMODE_REVERSE or not
   *
   * REVERSE vs forward mode requires a different search strategy,
   * *DEPEND alters the search somewhat and affects results printing. */

  datom = tree_pkg_atom(pkg_ctx, false);
  if (datom == NULL)
    return ret;

  if ((state->qmode & QMODE_REVERSE) == 0)
  {
    /* see if this cat/pkg is requested */
    array_for_each(state->atoms, i, atom)
    {
      if (atom->blocker != ATOM_BL_NONE ||
          atom->SLOT != NULL ||
          atom->REPO != NULL)
        datom = tree_pkg_atom(pkg_ctx, true);
      if (atom_compare(datom, atom) == EQUAL)
      {
        atom = NULL;
        break;
      }
    }

    /* nothing matched */
    if (atom != NULL)
      return ret;

    ret = 1;

    datom = tree_pkg_atom(pkg_ctx, true);
    printf("%s:", atom_format(state->format, datom));
  }

  clear_set(state->udeps);

#define get_depstr(X,Y) \
  X == QMODE_DEPEND  ? tree_pkg_meta(Y, Q_DEPEND)  : \
  X == QMODE_RDEPEND ? tree_pkg_meta(Y, Q_RDEPEND) : \
  X == QMODE_PDEPEND ? tree_pkg_meta(Y, Q_PDEPEND) : \
  X == QMODE_BDEPEND ? tree_pkg_meta(Y, Q_BDEPEND) : \
  tree_pkg_meta(Y, Q_IDEPEND) ;

  dfile = depend_files;
  for (i = QMODE_DEP_FIRST; i <= QMODE_DEP_LAST; i <<= 1, dfile++)
  {
    if (!(state->qmode & i))
      continue;

    depstr = get_depstr(i, pkg_ctx);
    if (depstr == NULL)
      continue;
    dep_tree = dep_grow_tree(depstr);
    if (dep_tree == NULL) {
      warn("failed to parse depstring from %s\n", atom_to_string(datom));
      continue;
    }

    deps = array_new();

    if (state->qmode & QMODE_TREE &&
        !(state->qmode & QMODE_REVERSE) &&
        verbose)
    {
      if (state->resolve)
      {
        set_t *use = NULL;
        array *ma  = tree_match_atom(state->vdb,
                                     datom,
                                     (TREE_MATCH_DEFAULT |
                                      TREE_MATCH_FIRST));
        if (array_cnt(ma) > 0)
        {
          tree_pkg_ctx *p = array_get(ma, 0);
          use = set_add_from_string(NULL, tree_pkg_meta(p, Q_USE));
        }
        dep_resolve_tree(dep_tree, state->vdb, use);
        array_free(ma);
        set_free(use);
      }
    }
    else
    {
      if (state->qmode & QMODE_FILTERUSE)
        dep_prune_use(dep_tree, ev_use);
      dep_flatten_tree(dep_tree, deps);
    }

    if (verbose) {
      if (state->qmode & QMODE_REVERSE)
      {
        array_for_each(deps, m, atom)
        {
          array_for_each(state->atoms, n, fatom)
          {
            if (atom_compare(atom, fatom) == EQUAL)
            {
              fatom = NULL;
              break;
            }
          }
          if (fatom == NULL)
          {
            atom = NULL;
            break;
          }
        }
        if (atom == NULL)
        {
          ret = 1;

          if (!firstmatch)
          {
            datom = tree_pkg_atom(pkg_ctx, true);
            printf("%s:", atom_format(state->format, datom));
          }
          firstmatch = true;

          printf("\n%s=\"\n", *dfile);
          dep_print_tree(stdout, dep_tree, 1, state->atoms,
                         RED, verbose > 1);
          printf("\"");
        }
      }
      else
      {
        /* try and resolve expressions to real package atoms */
        if (state->resolve)
          dep_resolve_tree(dep_tree, state->vdb, ev_use);

        printf("\n%s=\"\n", *dfile);
        dep_print_tree(stdout, dep_tree, 1, deps,
                       GREEN, verbose > 1);
        printf("\"");
      }
    }
    else
    {
      if (state->qmode & QMODE_REVERSE)
      {
        array_for_each(deps, m, atom)
        {
          array_for_each(state->atoms, n, fatom)
          {
            if (atom_compare(atom, fatom) == EQUAL)
            {
              fatom = NULL;
              break;
            }
          }
          if (fatom == NULL)
          {
            ret = 1;

            if (!firstmatch)
            {
              datom = tree_pkg_atom(pkg_ctx, true);
              printf("%s%s", atom_format(state->format, datom),
                     quiet < 2 ? ":" : "");
            }
            firstmatch = true;

            snprintf(buf, sizeof(buf), "%s%s%s",
                     RED, atom_to_string(atom), NORM);
            if (quiet < 2)
              state->udeps = add_set_unique(buf,
                                            state->udeps, NULL);
          }
          else if (!quiet)
          {
            state->udeps = add_set_unique(atom_to_string(atom),
                                          state->udeps, NULL);
          }
        }
      }
      else
      {
        array_for_each(deps, m, atom)
          state->udeps = add_set_unique(atom_to_string(atom),
                                        state->udeps, NULL);
      }
    }

    array_free(deps);
    dep_burn_tree(dep_tree);
  }
  if (verbose && ret == 1)
    printf("\n");

#undef get_depstr

  if (!verbose)
  {
    if ((state->qmode & QMODE_REVERSE) == 0 ||
        ret == 1)
    {
      char  *dep;
      deps = set_keys(state->udeps);
      array_for_each(deps, n, dep)
        printf(" %s", dep);
      printf("\n");
      array_free(deps);
    }
  }

  return ret;
}

int qdepends_main(int argc, char **argv)
{
  struct qdepends_opt_state state = {
    .atoms   = array_new(),
    .udeps   = create_set(),
    .qmode   = 0,
    .format  = "%[CATEGORY]%[PF]",
    .resolve = false,
    .vdb     = NULL,
  };
  atom_ctx *atom;
  size_t    i;
  int       ret;
  bool      do_pretty = false;

  if (quiet)
    state.format = "%[CATEGORY]%[PN]";

  while ((ret = GETOPT_LONG(QDEPENDS, qdepends, "")) != -1)
  {
    switch (ret)
    {
    COMMON_GETOPTS_CASES(qdepends)

    case 'd': state.qmode |= QMODE_DEPEND;    break;
    case 'r': state.qmode |= QMODE_RDEPEND;   break;
    case 'p': state.qmode |= QMODE_PDEPEND;   break;
    case 'b': state.qmode |= QMODE_BDEPEND;   break;
    case 'I': state.qmode |= QMODE_IDEPEND;   break;
    case 'Q': state.qmode |= QMODE_REVERSE;   break;
    case 'i': state.qmode |= QMODE_INSTALLED; break;
    case 't': state.qmode |= QMODE_TREE;      break;
    case 'U': state.qmode |= QMODE_FILTERUSE; break;
    case 'S': do_pretty = true;               break;
    case 'R': state.resolve = true;           break;
    case 'F': state.format = optarg;          break;
    }
  }

  if ((state.qmode & (QMODE_DEPEND  |
                      QMODE_RDEPEND |
                      QMODE_PDEPEND |
                      QMODE_BDEPEND |
                      QMODE_IDEPEND )) == 0)
  {
    /* default mode of printing: -drpb */
    state.qmode |= (QMODE_DEPEND  |
                    QMODE_RDEPEND |
                    QMODE_PDEPEND |
                    QMODE_BDEPEND |
                    QMODE_IDEPEND);
  }

  /* default to installed packages */
  if (!(state.qmode & QMODE_INSTALLED) &&
      !(state.qmode & QMODE_TREE))
    state.qmode |= QMODE_INSTALLED;

  /* don't allow both installed and from tree */
  if (state.qmode & QMODE_INSTALLED &&
      state.qmode & QMODE_TREE)
  {
    warn("-i and -t cannot be used together, dropping -i");
    state.qmode &= ~QMODE_INSTALLED;
  }

  if ((argc == optind) &&
      !do_pretty)
  {
    free_set(state.udeps);
    array_free(state.atoms);
    qdepends_usage(EXIT_FAILURE);
  }

  if (do_pretty)
  {
    ret = EXIT_SUCCESS;
    while (optind < argc)
    {
      if (!qdepends_print_depend(stdout, argv[optind++]))
      {
        ret = EXIT_FAILURE;
        break;
      }
      if (optind < argc)
        fprintf(stdout, "\n");
    }
    array_free(state.atoms);
    free_set(state.udeps);
    return ret;
  }

  argc -= optind;
  argv += optind;

  for (i = 0; i < (size_t)argc; i++)
  {
    atom = atom_explode(argv[i]);
    if (!atom)
      warn("invalid atom: %s", argv[i]);
    else
      array_append(state.atoms, atom);
  }

  if (state.qmode & QMODE_INSTALLED ||
      verbose)
  {
    state.vdb = tree_new(portroot, portvdb, TREETYPE_VDB, false);
    if (state.vdb == NULL)
    {
      free_set(state.udeps);
      array_deepfree(state.atoms, (array_free_cb *)atom_implode);
      err("failed to open VDB at %s", portvdb);
    }
  }

  ret = 0;
  if (state.qmode & QMODE_TREE)
  {
    tree_ctx *t;
    char     *overlay;
    size_t    n;

    array_for_each(overlays, n, overlay)
    {
      t = tree_new(portroot, overlay, TREETYPE_EBUILD, false);
      if (t != NULL) {
        if (!(state.qmode & QMODE_REVERSE) &&
            array_cnt(state.atoms) > 0)
        {
          array_for_each(state.atoms, i, atom)
          {
            ret |= tree_foreach_pkg_sorted(t,
                                           qdepends_results_cb, &state, atom);
          }
        }
        else
        {
          ret |= tree_foreach_pkg_sorted(t,
                                         qdepends_results_cb, &state, NULL);
        }
        tree_close(t);
      }
    }
  }
  else
  {  /* INSTALLED */
    if (!(state.qmode & QMODE_REVERSE) &&
        array_cnt(state.atoms) > 0)
    {
      array_for_each(state.atoms, i, atom)
      {
        ret |= tree_foreach_pkg_fast(state.vdb,
                                     qdepends_results_cb, &state, atom);
      }
    }
    else
    {
      ret |= tree_foreach_pkg_fast(state.vdb,
                                   qdepends_results_cb, &state, NULL);
    }
  }

  if (state.vdb != NULL)
    tree_close(state.vdb);
  if (state.depend != NULL)
    free(state.depend);

  array_deepfree(state.atoms, (array_free_cb *)atom_implode);
  free_set(state.udeps);

  if (!ret)
    warn("no matches found for your query");
  return ret ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
