/*
 * Copyright 2005-2019 Gentoo Foundation
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
#include <assert.h>
#include <ctype.h>

#include "atom.h"
#include "dep.h"
#include "set.h"
#include "xarray.h"
#include "xasprintf.h"

static const dep_node null_node = {
	.type = DEP_NULL,
};

static void _dep_attach(dep_node *root, dep_node *attach_me, int type);
static void _dep_burn_node(dep_node *node);

static dep_node *
_dep_grow_node(dep_type type, const char *info, size_t info_len)
{
	dep_node *ret;
	size_t len;

	if (type == DEP_OR || type == DEP_GROUP)
		info = NULL;

	len = sizeof(*ret);
	if (info) {
		if (!info_len)
			info_len = strlen(info);
		len += info_len + 1;
	}
	ret = xzalloc(len);

	ret->type = type;
	if (info) {
		ret->info = ((char*)ret) + sizeof(*ret);
		memcpy(ret->info, info, info_len);
		if (type == DEP_NORM)
			ret->atom = atom_explode(ret->info);
	}

	return ret;
}

static void
_dep_burn_node(dep_node *node)
{
	assert(node);
	if (node->info_on_heap)
		free(node->info);
	if (node->atom)
		atom_implode(node->atom);
	free(node);
}

enum {
	_DEP_NEIGH = 1,
	_DEP_CHILD = 2
};

static void
_dep_attach(dep_node *root, dep_node *attach_me, int type)
{
	if (type == _DEP_NEIGH) {
		if (!root->neighbor) {
			root->neighbor = attach_me;
			attach_me->parent = root->parent;
		} else
			_dep_attach(root->neighbor, attach_me, _DEP_NEIGH);
	} else {
		if (!root->children) {
			root->children = attach_me;
			attach_me->parent = root;
		} else
			_dep_attach(root->children, attach_me, _DEP_NEIGH);
	}
}

dep_node *
dep_grow_tree(const char *depend)
{
	bool saw_whitespace;
	signed long paren_balanced;
	const char *ptr, *word;
	int curr_attach;
	dep_node *ret, *curr_node, *new_node;
	dep_type prev_type;

	ret = curr_node = new_node = NULL;
	prev_type = DEP_NULL;
	paren_balanced = 0;
	curr_attach = _DEP_NEIGH;
	word = NULL;

#define _maybe_consume_word(t) \
	do { \
		if (word == NULL) \
			break; \
		new_node = _dep_grow_node(t, word, ptr-word); \
		if (!ret) \
			ret = curr_node = new_node; \
		else { \
			_dep_attach(curr_node, new_node, curr_attach); \
			curr_attach = _DEP_NEIGH; \
			curr_node = new_node; \
		} \
		prev_type = t; \
		word = NULL; \
	} while (0)

	saw_whitespace = true;
	for (ptr = depend; *ptr != '\0'; ptr++) {
		if (isspace((int)*ptr)) {
			saw_whitespace = true;
			_maybe_consume_word(DEP_NORM);
			continue;
		}

		switch (*ptr) {
		case '?': {
			if (word == NULL) {
				warnf("Found a ? but no USE flag");
				goto error_out;
			}
			_maybe_consume_word(DEP_USE);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '|': {
			if (!saw_whitespace)
				break;
			if (ptr[1] != '|') {
				warnf("Found a | but not ||");
				goto error_out;
			}
			word = ptr++;
			_maybe_consume_word(DEP_OR);
			curr_attach = _DEP_CHILD;
			continue;
		}
		case '(': {
			++paren_balanced;
			if (!saw_whitespace)
				break;
			if (prev_type == DEP_OR || prev_type == DEP_USE) {
				_maybe_consume_word(DEP_NORM);
				prev_type = DEP_NULL;
			} else {
				if (word) {
					warnf("New group has word in queue");
					goto error_out;
				}
				word = ptr;
				_maybe_consume_word(DEP_GROUP);
				curr_attach = _DEP_CHILD;
			}
			break;
		}
		case ')': {
			--paren_balanced;
			if (!saw_whitespace)
				break;
			_maybe_consume_word(DEP_NORM);

			if (curr_node->parent == NULL) {
				warnf("Group lacks a parent");
				goto error_out;
			}
			curr_node = curr_node->parent;
			curr_attach = _DEP_NEIGH;
			break;
		}
		case '[': {
			/* USE-dep, seek to matching ']', since they cannot be
			 * nested, this is simple */
			while (*ptr != '\0' && *ptr != ']')
				ptr++;
			break;
		}
		default:
			if (!word)
				word = ptr;
		}
		saw_whitespace = false;

		/* fall through to the paren failure below */
		if (paren_balanced < 0)
			break;
	}

	if (paren_balanced != 0) {
		warnf("Parenthesis unbalanced");
		goto error_out;
	}

	/* if the depend buffer wasnt terminated with a space,
	 * we may have a word sitting in the buffer to consume */
	_maybe_consume_word(DEP_NORM);

#undef _maybe_consume_word

	return ret ? : xmemdup(&null_node, sizeof(null_node));

error_out:
	warnf("DEPEND: %s", depend);
	if (ret) {
		dep_dump_tree(ret);
		dep_burn_tree(ret);
	}
	return NULL;
}

void
dep_print_tree(
		FILE *fp,
		const dep_node *root,
		size_t space,
		array_t *hlatoms,
		const char *hlcolor,
		int verbose)
{
	size_t s;
	int indent = 4;  /* Gentoo 4-wide indent standard */
	depend_atom *d = NULL;

	assert(root);
	if (root->type == DEP_NULL)
		goto this_node_sucks;

	for (s = space; s; --s)
		fprintf(fp, "%*s", indent, "");

	if (verbose > 0)
		fprintf(fp, "Node [%s]: ", _dep_names[root->type]);
	/*printf("Node %p [%s] %p %p %p: ", root, _dep_names[root->type], root->parent, root->neighbor, root->children);*/
	if (root->type == DEP_OR)
		fprintf(fp, "|| (");
	if (root->info) {
		if (hlatoms != NULL && array_cnt(hlatoms) > 0 &&
				root->type == DEP_NORM)
		{
			size_t i;
			depend_atom *m;
			char *oslot;

			d = root->atom;
			d->pfx_op = d->sfx_op = ATOM_OP_NONE;

			array_for_each(hlatoms, i, m) {
				oslot = d->SLOT;
				if (m->SLOT == NULL)
					d->SLOT = NULL;

				if (atom_compare(m, d) == EQUAL) {
					m = NULL;
					break;
				}
				d->SLOT = oslot;
			}

			if (m == NULL) { /* match found */
				fprintf(fp, "%s%s%s", hlcolor, root->info, NORM);
			} else {
				fprintf(fp, "%s", root->info);
			}
		} else {
			fprintf(fp, "%s", root->info);
		}
		/* If there is only one child, be nice to one-line: foo? ( pkg ) */
		if (root->type == DEP_USE)
			fprintf(fp, "? (");
	}
	fprintf(fp, "\n");

	if (root->children)
		dep_print_tree(fp, root->children, space+1, hlatoms, hlcolor, verbose);

	if (root->type == DEP_OR || root->type == DEP_USE) {
		for (s = space; s; --s)
			fprintf(fp, "%*s", indent, "");
		fprintf(fp, ")\n");
	}
 this_node_sucks:
	if (root->neighbor)
		dep_print_tree(fp, root->neighbor, space, hlatoms, hlcolor, verbose);
}

void
dep_burn_tree(dep_node *root)
{
	assert(root);
	if (root->children)
		dep_burn_tree(root->children);
	if (root->neighbor)
		dep_burn_tree(root->neighbor);
	_dep_burn_node(root);
}

void
dep_prune_use(dep_node *root, set *use)
{
	if (root->neighbor)
		dep_prune_use(root->neighbor, use);
	if (root->type == DEP_USE) {
		bool invert = (root->info[0] == '!' ? 1 : 0);
		bool notfound = contains_set(root->info + (invert ? 1 : 0), use);

		if (notfound ^ invert) {
			root->type = DEP_NULL;
			return;
		}
	}
	if (root->children)
		dep_prune_use(root->children, use);
}

void
dep_flatten_tree(const dep_node *root, array_t *out)
{
	if (root->type != DEP_NULL) {
		if (root->type == DEP_NORM)
			xarraypush_ptr(out, root->atom);
		if (root->children)
			dep_flatten_tree(root->children, out);
	}
	if (root->neighbor)
		dep_flatten_tree(root->neighbor, out);
}
