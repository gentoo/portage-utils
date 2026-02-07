/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <xalloc.h>
#include <ctype.h>

#include "array.h"
#include "atom.h"
#include "dep.h"
#include "set.h"
#include "tree.h"

/* dep_prune_use: mutilates the tree, setting nodes to NULL that are not
 * active -> should it return a new tree instead?
 * dep_resolve_tree: looks up atoms in the tree and populates pkg
 * pointer for a matching tree_pkg_ctx, doesn't do anything with ||,
 * leaves nodes alone that don't resolve -> useless?
 *
 * needed functionality:
 * - prune phase -> remove use-dep-groups that are not active
 *                  downgrade use-dep nodes into all-group nodes
 * - resolving   -> simple match of atom to a package from a tree
 *                  - consider multiple trees, in priorities
 *                  any-groups downgraded to all-group with the first
 *                  block from the any-group that matches
 * - flattening  -> assume all reductions to be done that one wants
 *                  remove all groups (any, all, use) and return the atom
 *                  nodes in an array
 */

#define DEP_TYPES(X) \
  X(NULL) \
  X(ATOM) \
  X(USE) \
  X(ANY) \
  X(ALL) \
  X(NOT) \
  X(HUH) \
  X(POPEN) \
  X(PCLOSE) \
  X(WORD)

#define DEP_TYPE_ENUM(E)  DEP_##E,
#define DEP_TYPE_NAME(E)  #E,

typedef enum dep_type_ {
  DEP_TYPES(DEP_TYPE_ENUM)
  DEP_MAX_TYPES
} dep_type_t;

static const char * const dep_type_names[] = {
  DEP_TYPES(DEP_TYPE_NAME)
  NULL
};

struct dep_node_ {
  const char       *word;
  size_t            wordlen;
  depend_atom      *atom;
  tree_pkg_ctx     *pkg;
  dep_node_t       *parent;
  array            *members;
  dep_type_t        type;
  bool              invert:1;
};

dep_node_t *dep_grow_tree
(
  const char *depend
)
{
  char        buf[_Q_PATH_MAX];
  dep_node_t *ret             = NULL;
  dep_node_t *curr_node;
  dep_node_t *next_node;
  dep_node_t *final_node;
  array      *tokens          = array_new();
  array      *res             = array_new();
  const char *ptr             = NULL;
  const char *word;
  size_t      n;
  int         level;
  int         nots            = 0;

  /* the language is mostly token oriented, and officially whitespace is
   * required around tokens, but we try to parse very liberal,
   * because there's no real ambiguity
   * constructs: (from PMS)
   * - [!]use? ( ... )    DEP_USE    (use-conditional group)
   * - || ( ... )         DEP_ANY    (any-of group)
   * - ( ... )            DEP_ALL    (all-of group)
   * - foo-bar/fnord-x.y  DEP_ATOM   (package dependency specification)
   * we first tokenise the input, then try to parse it */

#define dep_push_word(W,L) \
  do { \
    dep_node_t *new_node = xzalloc(sizeof(*new_node)); \
    new_node->type    = DEP_WORD; \
    new_node->word    = W; \
    new_node->wordlen = L; \
    array_append(tokens, new_node); \
  } while (0)
#define dep_push(T) \
  do { \
    dep_node_t *new_node = xzalloc(sizeof(*new_node)); \
    new_node->type    = DEP_##T; \
    array_append(tokens, new_node); \
  } while (0)

  for (ptr = word = depend; *ptr != '\0'; ptr++)
  {
    char p = *ptr;
    if (isspace((int)p))
      p = ' ';  /* simplify switch below */

    switch (p)
    {
    case ' ':
      if (word != ptr)
        dep_push_word(word, ptr - word);
      break;
    case '!':
      if (word != ptr)
        dep_push_word(word, ptr - word);
      dep_push(NOT);
      break;
    case '|':
      if (ptr[1] != '|')
      {
        warn("Found a |, did you mean ||? (in %s)", depend);
        goto dep_grow_tree_fail;
      }
      if (word != ptr)
        dep_push_word(word, ptr - word);
      ptr++;
      dep_push(ANY);
      break;
    case '?':
      if (word != ptr)
        dep_push_word(word, ptr - word);
      dep_push(HUH);
      break;
    case '(':
      if (word != ptr)
        dep_push_word(word, ptr - word);
      dep_push(POPEN);
      break;
    case ')':
      if (word != ptr)
        dep_push_word(word, ptr - word);
      dep_push(PCLOSE);
      break;
    case '[':
      /* these are atom's USE-conditionals which may include ( ) ? !
       * too, so just scan until matching ], luckily these can't be
       * nested -- except when we get unexpanded bash stuff like
       * ${LIB_DEPEND//[static-libs([+-])]} it does, so check it */
      level = 0;
      for (ptr++; *ptr != '\0'; ptr++)
      {
        if (*ptr == ']')
        {
          if (level == 0)
            break;
          else
            level--;
        }
        else if (*ptr == '[')
        {
          level++;
        }
      }
      ptr--;  /* rewind for outer for-loop */
      continue;
    default:
      /* count as word */
      continue;
    }

    word = ptr + 1;
  }
  /* push final word, if there is one (like a single atom as depstring) */
  if (word != ptr)
    dep_push_word(word, ptr - word);
#undef dep_push_word
#undef dep_push

#ifdef EBUG
  array_for_each(tokens, n, curr_node)
  {
    warnf("token %s %.*s",
          dep_type_names[curr_node->type],
          (int)curr_node->wordlen,
          curr_node->word != NULL ? curr_node->word : "");
  }
#endif

  /* create top node */
  ret = xzalloc(sizeof(*ret));
  ret->type    = DEP_ALL;
  ret->members = array_new();
  array_append(res, ret);  /* == level */
  ret   = NULL;
  level = 0;

  array_for_each(tokens, n, curr_node)
  {
    DBG("n: %zu, %s, level %d", n, dep_type_names[curr_node->type], level);
    switch (curr_node->type)
    {
    case DEP_POPEN:
      ret = xzalloc(sizeof(*ret));
      ret->type    = DEP_ALL;
      ret->members = array_new();
      ret->parent  = array_get(res, level);
      if (ret->parent == NULL)
      {
        warnf("Internal error, missing node for level %d", level);
        dep_burn_tree(ret);
        ret = NULL;
        goto dep_grow_tree_fail;
      }
      array_append(ret->parent->members, ret);

      array_append(res, ret);
      level++;
      break;
    case DEP_PCLOSE:
      if (level > 0)
      {
        array_remove(res, level);
        level--;
      }
      else
      {
        warn("Found stray ) (in %s)", depend);
        ret = NULL;
        goto dep_grow_tree_fail;
      }
      break;
    case DEP_ANY:
      next_node = array_get(tokens, n + 1);
      if (next_node == NULL ||
          next_node->type != DEP_POPEN)
      {
        if (next_node == NULL)
          warn("Missing ( after ||, truncated data? (in %s)", depend);
        else
          warn("Missing ( after ||, got %s (in %s)",
               dep_type_names[next_node->type], depend);
        ret = NULL;
        goto dep_grow_tree_fail;
      }

      ret = xzalloc(sizeof(*ret));
      ret->type    = DEP_ANY;
      ret->members = array_new();
      ret->parent  = array_get(res, level);
      if (ret->parent == NULL)
      {
        warnf("Internal error, missing node for level %d", level);
        dep_burn_tree(ret);
        ret = NULL;
        goto dep_grow_tree_fail;
      }
      array_append(ret->parent->members, ret);

      array_append(res, ret);
      n++;  /* skip POPEN */
      level++;
      break;
    case DEP_NOT:
      nots = 1;
      next_node = array_get(tokens, n + 1);
      /* !! is hard blocker for atom syntax */
      if (next_node != NULL &&
          next_node->type == DEP_NOT)
      {
        nots = 2;
        n++;
        next_node = array_get(tokens, n + 1);
      }

      if (next_node == NULL ||
          next_node->type != DEP_WORD)
      {
        warn("Found dangling ! (in %s)", depend);
        ret = NULL;
        goto dep_grow_tree_fail;
      }

      break;
    case DEP_WORD:
      next_node = array_get(tokens, n + 1);
      if (next_node != NULL &&
          next_node->type == DEP_HUH)
      {
        /* must be USE */
        if (nots > 1)
        {
          warn("Too many !s for use? (in %s)", depend);
          ret = NULL;
          goto dep_grow_tree_fail;
        }

        final_node = array_get(tokens, n + 2);
        if (final_node == NULL ||
            final_node->type != DEP_POPEN)
        {
          warn("Missing ( after use? (in %s)", depend);
          ret = NULL;
          goto dep_grow_tree_fail;
        }
        else
        {
          ret = xzalloc(sizeof(*ret) + curr_node->wordlen + 1);
          ret->type    = DEP_USE;
          ret->members = array_new();
          ret->invert  = nots == 1;
          ret->word    = (char *)ret + sizeof(*ret);
          ret->wordlen = curr_node->wordlen;
          memcpy((char *)ret + sizeof(*ret),
                 curr_node->word, curr_node->wordlen);
          ((char *)ret)[sizeof(*ret) + ret->wordlen] = '\0';
          ret->parent  = array_get(res, level);
          if (ret->parent == NULL)
          {
            warnf("Internal error, missing node for level %d", level);
            dep_burn_tree(ret);
            ret = NULL;
            goto dep_grow_tree_fail;
          }
          array_append(ret->parent->members, ret);

          array_append(res, ret);
          n += 2;
          level++;
        }
      }
      else
      {
        /* atom, WORD */
        snprintf(buf, sizeof(buf), "%s%s%.*s",
                 nots > 0 ? "!" : "",
                 nots > 1 ? "!" : "",
                 (int)curr_node->wordlen, curr_node->word);

        ret = xzalloc(sizeof(*ret));
        ret->type    = DEP_ATOM;
        ret->atom    = atom_explode(buf);
        ret->parent  = array_get(res, level);
        if (ret->parent == NULL)
        {
          warnf("Internal error, missing node for level %d", level);
          dep_burn_tree(ret);
          ret = NULL;
          goto dep_grow_tree_fail;
        }
        array_append(ret->parent->members, ret);
      }
      nots = 0;
      break;
    default:
      /* ignore/invalid */
      break;
    }
  }

#ifdef EBUG
  array_for_each(res, n, ret)
  {
    warnf("[%zu] token %s %.*s (%zu)",
          n,
          dep_type_names[ret->type],
          (int)ret->wordlen,
          ret->word != NULL ? ret->word : "",
          ret->members == NULL ? 0 : array_cnt(ret->members));
  }
#endif

  ret = array_remove(res, 0);  /* pseudo top-level again */

dep_grow_tree_fail:
  array_deepfree(tokens, (array_free_cb *)dep_burn_tree);
  array_free(res);

  if (ret != NULL &&
      ret->members != NULL &&
      array_cnt(ret->members) == 0)
    ret->type = DEP_NULL;

  return ret;
}

static void dep_print_tree_int
(
  FILE             *fp,
  const dep_node_t *root,
  size_t            space,
  array            *hlatoms,
  const char       *hlcolor,
  int               verbose,
  bool              first
)
{
  dep_node_t *memb;
  size_t      s;
  int         indent      = 4;      /* Gentoo 4-wide indent standard */
  bool        newline     = true;

  if (root == NULL)
    return;

  if (verbose < 0)
  {
    newline = false;
    verbose = -verbose - 1;
  }

  if (verbose > 0)
    fprintf(fp, "Node [%s]: ", dep_type_names[root->type]);

  if (!newline)
  {
    if (!first > 0)
      fprintf(fp, " ");
  }
  else
  {
    if (!first > 0)
      fprintf(fp, "\n");
    for (s = space; s > 0; s--)
      fprintf(fp, "%*s", indent, "");
  }

  if (root->type == DEP_ANY)
  {
    fprintf(fp, "|| ");
  }
  else if (root->type == DEP_USE)
  {
    fprintf(fp, "%s? ", root->word);
  }
  else if (root->type == DEP_ATOM)
  {
    atom_ctx *a     = root->atom;
    bool      match = false;

    if (root->pkg != NULL)
    {
      a = tree_pkg_atom(root->pkg, false);
      if (hlatoms == NULL)
        match = true;
    }

    if (hlatoms != NULL)
    {
      size_t       i;
      depend_atom *m;

      array_for_each(hlatoms, i, m)
      {
        /* make m query, such that any specifics (SLOT,
         * pfx/sfx) from the depstring are ignored while
         * highlighting */
        if (atom_compare(a, m) == EQUAL)
        {
          match = true;
          break;
        }
      }
    }

    fprintf(fp, "%s%s%s",
            match ? hlcolor : "",
            atom_to_string(a),
            match ? NORM : "");
  }

  if (root->type == DEP_ANY ||
      root->type == DEP_USE ||
      root->type == DEP_ALL)
  {
    bool singlechild = false;

    /* print on single line, when it's just one atom */
    if (array_cnt(root->members) == 1 &&
        ((dep_node_t *)array_get(root->members, 0))->type == DEP_ATOM)
      singlechild = true;

    /* write leading space in singlechild mode, because we set indent
     * level to 0 in that case, which suppresses the leading space, for
     * it assumes that's the start of the output */
    fprintf(fp, "(%s", singlechild ? " " : "");

    array_for_each(root->members, s, memb)
    {
      dep_print_tree_int(fp,
                         memb,
                         singlechild ? 0 : space + 1,
                         hlatoms,
                         hlcolor,
                         singlechild ? -verbose - 1 : verbose,
                         singlechild ? true : false);
    }

    if (singlechild ||
        !newline)
    {
      fprintf(fp, " )");
    }
    else
    {
      fprintf(fp, "\n");
      for (s = space; s; s--)
        fprintf(fp, "%*s", indent, "");
      fprintf(fp, ")");
    }
  }
}

void dep_print_tree
(
  FILE             *fp,
  const dep_node_t *root,
  size_t            space,
  array            *hlatoms,
  const char       *hlcolor,
  int               verbose
)
{
  dep_node_t *memb;
  size_t      s;

  if (root == NULL)
    return;

  /* simplify checks from here on out */
  if (hlatoms != NULL &&
      array_cnt(hlatoms) == 0)
    hlatoms = NULL;

  array_for_each(root->members, s, memb)
    dep_print_tree_int(fp, memb, space, hlatoms, hlcolor, verbose, s == 0);

  if (verbose >= 0 &&
      array_cnt(root->members) > 0)
    fprintf(fp, "\n");

  return;
}

void dep_burn_tree
(
  dep_node_t *root
)
{
  if (root == NULL)
    return;

  if (root->members != NULL)
    array_deepfree(root->members, (array_free_cb *)dep_burn_tree);

  if (root->atom)
    atom_implode(root->atom);

  free(root);
}

/* eliminate all DEP_USE nodes in the dep tree, nodes that do not match
 * the set of given USE words are removed, matching ones are converted
 * to DEP_ALL nodes, this takes into account negatives/inverted
 * conditionals */
void dep_prune_use
(
  dep_node_t *root,
  set_t      *use
)
{
  if (root->type == DEP_USE)
  {
    bool found = set_contains(use, root->word);

    if (!found ^ root->invert)
    {
      /* it's hard to free the node here, because we have no return to
       * flag the parent to remove it from its member list, so we flag
       * it to be skipped next time */
      root->type = DEP_NULL;
      return;
    }
    else
    {
      /* this node is "active", so turn it into a regular one */
      root->type = DEP_ALL;
    }
  }

  if (root->members != NULL)
  {
    dep_node_t *memb;
    size_t      n;

    array_for_each(root->members, n, memb)
      dep_prune_use(memb, use);
  }
}

void dep_resolve_tree
(
  dep_node_t *root,
  tree_ctx   *t
)
{
  if (root->type == DEP_NULL)
    return;  /* avoid recursing below */

  if (root->type == DEP_ATOM &&
      root->atom &&
      root->pkg == NULL)
  {
    atom_ctx *d = root->atom;
    array    *r = tree_match_atom(t, d, (TREE_MATCH_DEFAULT |
                                         TREE_MATCH_LATEST));
    if (array_cnt(r) > 0)
      root->pkg = array_get(r, 0);
    array_free(r);
  }

  if (root->members)
  {
    dep_node_t *memb;
    size_t      n;

    array_for_each(root->members, n, memb)
      dep_resolve_tree(memb, t);
  }
}

/* drop any (USE-)indirections and add all atoms in the dep node to the
 * array pointed to by out
 * because there is no knowledge here of the tree, ANY nodes are treated
 * as ALL nodes, e.g. all of their atoms are added
 * use dep_prune_tree to eliminate USE-conditionals */
void dep_flatten_tree
(
  dep_node_t *root,
  array      *out
)
{
  if (root->type == DEP_NULL ||
      out == NULL)
    return;

  if (root->members != NULL)
  {
    dep_node_t *memb;
    size_t      n;

    array_for_each(root->members, n, memb)
      dep_flatten_tree(memb, out);
  }
  else if (root->atom != NULL)
  {
    array_append(out, root->atom);
  }
}

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
