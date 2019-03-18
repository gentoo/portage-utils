/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2016 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifndef _ATOM_COMPARE_H
#define _ATOM_COMPARE_H 1

typedef enum {
	VER_ALPHA=0, VER_BETA, VER_PRE, VER_RC, VER_NORM, VER_P
} atom_suffixes;

extern const char * const atom_suffixes_str[];

typedef struct {
	atom_suffixes suffix;
	uint64_t sint;
} atom_suffix;

extern const char * const atom_op_str[];

typedef enum {
	/*    */ ATOM_OP_NONE = 0,
	/* >  */ ATOM_OP_NEWER,
	/* >= */ ATOM_OP_NEWER_EQUAL,
	/* =  */ ATOM_OP_EQUAL,
	/* <= */ ATOM_OP_OLDER_EQUAL,
	/* <  */ ATOM_OP_OLDER,
	/* ~  */ ATOM_OP_PV_EQUAL,
	/* !  */ ATOM_OP_BLOCK,
	/* !! */ ATOM_OP_BLOCK_HARD,
	/* *  */ ATOM_OP_STAR,
} atom_operator;

typedef struct {
	/* XXX: we don't provide PF ... */
	atom_operator pfx_op, sfx_op;
	char *CATEGORY;
	char *PN;
	unsigned int PR_int;
	char letter;
	atom_suffix *suffixes;
	char *PV, *PVR;
	char *P, *SLOT, *REPO;
} depend_atom;

extern const char * const booga[];
enum { ERROR=0, NOT_EQUAL, EQUAL, NEWER, OLDER };

depend_atom *atom_explode(const char *atom);
void atom_implode(depend_atom *atom);
int atom_compare(const depend_atom *a1, const depend_atom *a2);
int atom_compare_str(const char * const s1, const char * const s2);

#endif
