/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _ATOM_COMPARE_H
#define _ATOM_COMPARE_H 1

typedef enum {
	VER_ALPHA=0, VER_BETA, VER_PRE, VER_RC, VER_NORM, VER_P
} atom_suffixes;
extern const char * const atom_suffixes_str[];

/* slotdeps, := :* :SLOT= */
typedef enum {
	/*   */ ATOM_SD_NONE = 0,
	/* = */ ATOM_SD_ANY_REBUILD,
	/* * */ ATOM_SD_ANY_IGNORE,
} atom_slotdep;
extern const char * const atom_slotdep_str[];

typedef enum {
	/*     */ ATOM_UC_NONE = 0,
	/* !   */ ATOM_UC_NOT,
	/* -   */ ATOM_UC_NEG,
	/* ?   */ ATOM_UC_COND,
	/* =   */ ATOM_UC_EQUAL,
	/* (+) */ ATOM_UC_PREV_ENABLED,
	/* (-) */ ATOM_UC_PREV_DISABLED,
} atom_usecond;
extern const char * const atom_usecond_str[];

typedef enum {
	/*    */ ATOM_BL_NONE = 0,
	/* !  */ ATOM_BL_BLOCK,
	/* !! */ ATOM_BL_BLOCK_HARD,
	/* ^  */ ATOM_BL_ANTISLOT,
} atom_blocker;
extern const char * const atom_blocker_str[];

typedef enum {
	/*    */ ATOM_OP_NONE = 0,
	/* =  */ ATOM_OP_EQUAL,
	/* >  */ ATOM_OP_NEWER,
	/* >= */ ATOM_OP_NEWER_EQUAL,
	/* <  */ ATOM_OP_OLDER,
	/* <= */ ATOM_OP_OLDER_EQUAL,
	/* ~  */ ATOM_OP_PV_EQUAL,
	/* *  */ ATOM_OP_STAR,
	/*    */ ATOM_OP_NEQUAL,
} atom_operator;
extern const char * const atom_op_str[];

typedef struct {
	atom_suffixes suffix;
	uint64_t sint;
} atom_suffix;

typedef struct _atom_usedep {
	struct _atom_usedep *next;
	char *use;
	atom_usecond pfx_cond;
	atom_usecond sfx_cond;
} atom_usedep;

typedef struct {
	atom_blocker blocker;
	atom_operator pfx_op;
	atom_operator sfx_op;
	char *CATEGORY;
	char *PN;
	char *PV;
	char *PF;
	unsigned int PR_int;
	char letter;
	atom_suffix *suffixes;
	char *PVR;
	char *P;
	atom_usedep *usedeps;
	char *SLOT;
	char *SUBSLOT;
	atom_slotdep slotdep;
	char *REPO;
} depend_atom;

extern const char * const booga[];
typedef enum {
	ERROR = 0,
	NOT_EQUAL,
	EQUAL,
	NEWER,
	OLDER
} atom_equality;

depend_atom *atom_explode(const char *atom);
depend_atom *atom_clone(depend_atom *atom);
void atom_implode(depend_atom *atom);
atom_equality atom_compare(const depend_atom *a1, const depend_atom *a2);
atom_equality atom_compare_str(const char * const s1, const char * const s2);
char *atom_to_string_r(char *buf, size_t buflen, depend_atom *a);
char *atom_format_r(char *buf, size_t buflen,
		const char *format, const depend_atom *atom);
char *atom_to_string(depend_atom *a);
char *atom_format(const char *format, const depend_atom *atom);
int atom_compar_cb(const void *l, const void *r);

#endif
