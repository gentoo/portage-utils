/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _DEP_H
#define _DEP_H 1

#include "array.h"
#include "atom.h"
#include "colors.h"
#include "set.h"
#include "tree.h"

typedef struct dep_node_ dep_node_t;

/* prototypes */
dep_node_t *dep_grow_tree(const char *depend);
void        dep_print_tree(FILE *fp, const dep_node_t *root, size_t space, array *m, const char *c, int verbose);
void        dep_resolve_tree(dep_node_t *root, tree_ctx *t);
void        dep_burn_tree(dep_node_t *root);
void        dep_prune_use(dep_node_t *root, set *use);
void        dep_flatten_tree(dep_node_t *root, array *out);

/* 2026 API boring (but predictable) names for grow/burn */
#define dep_new_tree(D)  dep_grow_tree(D)
#define dep_free_tree(D) dep_burn_tree(D)

#endif

/* vim: set ts=2 sw=2 expandtab cino+=\:0 foldmethod=marker: */
