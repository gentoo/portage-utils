/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _TREE_H
#define _TREE_H 1

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>

#include "atom.h"
#include "set.h"

typedef struct tree_             tree_ctx;
typedef struct tree_cat_         tree_cat_ctx;
typedef struct tree_pkg_         tree_pkg_ctx; 
typedef struct tree_metadata_xml tree_metadata_xml;

struct tree_metadata_xml {
  array *email;
};

/* foreach pkg callback function signature */
typedef int (tree_pkg_cb)(tree_pkg_ctx *, void *priv);

/* these are the functional type of trees we can open, availability of
 * metadata and so on is determined automatically when feasible */
enum tree_open_type {
  /* deliberately no 0 so default input is invalid */
  TREETYPE_EBUILD = 1,
  TREETYPE_VDB,
  TREETYPE_BINPKG,
  TREETYPE_GTREE,
};

/* metadata keys known to us available for retrieval */
#define TREE_META_KEYS(X) \
  X(DEPEND) \
  X(RDEPEND) \
  X(SLOT) \
  X(SRC_URI) \
  X(RESTRICT) \
  X(HOMEPAGE) \
  X(LICENSE) \
  X(DESCRIPTION) \
  X(KEYWORDS) \
  X(INHERITED) \
  X(IUSE) \
  X(CDEPEND) \
  X(PDEPEND) \
  X(PROVIDE) \
  X(EAPI) \
  X(PROPERTIES) \
  X(BDEPEND) \
  X(IDEPEND) \
  X(DEFINED_PHASES) \
  X(REQUIRED_USE) \
  X(CONTENTS) \
  X(USE) \
  X(EPREFIX) \
  X(PATH) \
  X(BUILD_ID) \
  X(repository) \
  X(MD5) \
  X(SHA1) \
  X(SIZE) \
  X(_eclasses_) \
  X(_md5_)

#define TREE_META_KEY_ENUM(E)  Q_##E,
#define TREE_META_KEY_NAME(E)  #E,

enum tree_pkg_meta_keys {
  Q_UNKNOWN = 0,
  TREE_META_KEYS(TREE_META_KEY_ENUM)
  TREE_META_MAX_KEYS
};

tree_ctx           *tree_new(const char *portroot, const char *path,
                             enum tree_open_type type, bool quiet);
tree_ctx           *tree_merge(tree_ctx *tree1, tree_ctx *tree2);
void                tree_close(tree_ctx *tree);

int                 tree_foreach_pkg(tree_ctx *tree, tree_pkg_cb callback,
                                     void *priv, bool sorted,
                                     const atom_ctx *query);
#define tree_foreach_pkg_fast(ctx, cb, priv, query) \
	tree_foreach_pkg(ctx, cb, priv, false, query)
#define tree_foreach_pkg_sorted(ctx, cb, priv, query) \
	tree_foreach_pkg(ctx, cb, priv, true, query)

array              *tree_match_atom(tree_ctx *tree, const atom_ctx *query,
                                    int flags);
#define TREE_MATCH_LATEST     (1<<3)
#define TREE_MATCH_FIRST      (1<<4)
#define TREE_MATCH_VIRTUAL    (1<<5)
#define TREE_MATCH_ACCT       (1<<6)
#define TREE_MATCH_SORT       (1<<7)
#define TREE_MATCH_DEFAULT    (TREE_MATCH_VIRTUAL | \
                               TREE_MATCH_ACCT    | \
                               TREE_MATCH_SORT    )

tree_metadata_xml  *tree_pkg_metadata(tree_pkg_ctx *pkg_ctx);
void                tree_close_metadata(tree_metadata_xml *meta_ctx);

char               *tree_get_repo_name(tree_ctx *tree);
char               *tree_get_path(tree_ctx *tree);
int                 tree_get_portroot_fd(tree_ctx *tree);
enum tree_open_type tree_get_treetype(tree_ctx *tree);

char               *tree_pkg_meta(tree_pkg_ctx *pkg,
                                  enum tree_pkg_meta_keys key);
atom_ctx           *tree_pkg_atom(tree_pkg_ctx *pkg, bool full);
tree_ctx           *tree_pkg_get_tree(tree_pkg_ctx *pkg);
char               *tree_pkg_get_cat_name(tree_pkg_ctx *pkg);
char               *tree_pkg_get_pf_name(tree_pkg_ctx *pkg);
char               *tree_pkg_get_path(tree_pkg_ctx *pkg);
int                 tree_pkg_get_portroot_fd(tree_pkg_ctx *pkg);
enum tree_open_type tree_pkg_get_treetype(tree_pkg_ctx *pkg);

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
