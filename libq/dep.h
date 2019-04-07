/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _DEP_H
#define _DEP_H 1

#include "atom.h"
#include "colors.h"
#include "set.h"
#include "xarray.h"

typedef enum {
	DEP_NULL = 0,
	DEP_NORM = 1,
	DEP_USE = 2,
	DEP_OR = 3,
	DEP_GROUP = 4
} dep_type;

static const char * const _dep_names[] = {
	"NULL",
	"NORM",
	"USE",
	"OR",
	"GROUP"
};

struct _dep_node {
	dep_type type;
	char *info;
	char info_on_heap;
	depend_atom *atom;
	struct _dep_node *parent;
	struct _dep_node *neighbor;
	struct _dep_node *children;
};
typedef struct _dep_node dep_node;

/* prototypes */
#ifdef NDEBUG
# define dep_dump_tree(r)
#else
# define dep_dump_tree(r) dep_print_tree(stdout, r, 0, NULL, NORM, 0)
#endif

dep_node *dep_grow_tree(const char *depend);
void dep_print_tree(FILE *fp, const dep_node *root, size_t space, array_t *m, const char *c, int verbose);
void dep_burn_tree(dep_node *root);
void dep_prune_use(dep_node *root, set *use);
void dep_flatten_tree(const dep_node *root, array_t *out);

#endif
