/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _DEP_H
#define _DEP_H 1

#include <unistd.h>

#include "array.h"
#include "atom.h"
#include "colors.h"
#include "set.h"
#include "tree.h"

typedef struct dep_node_ dep_node_t;
typedef enum dep_status_ dep_status_t;

enum dep_status_ {
  DEP_OK = 1,
  DEP_FAIL,
  DEP_NEWBLOCKER,
};

/* prototypes */
dep_node_t   *dep_grow_tree(const char *depend);
void          dep_print_tree(FILE *fp, const dep_node_t *root, size_t space,
                             array *m, const char *c, int verbose);
dep_status_t  dep_resolve_tree(dep_node_t *root, tree_ctx *t,
                               set_t *use, hash_t *blockers);
void          dep_prune_use(dep_node_t *root, set_t *use);
array        *dep_flatten_tree(dep_node_t *root);
void          dep_burn_tree(dep_node_t *root);

/* 2026 API boring (but predictable) names */
#define dep_new(D)              dep_grow_tree(D)
#define dep_print(F,D,S,M,C,V)  dep_print_tree(F,D,S,M,C,V)
#define dep_resolve(D,T,U,B)    dep_resolve_tree(D,T,U,B)
#define dep_prune(D,U)          dep_prune_use(D,U)
#define dep_flatten(D)          dep_flatten_tree(D);
#define dep_free(D)             dep_burn_tree(D)

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
