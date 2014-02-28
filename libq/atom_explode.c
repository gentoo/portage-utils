/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

typedef enum { VER_ALPHA=0, VER_BETA, VER_PRE, VER_RC, VER_NORM, VER_P } atom_suffixes;
const char * const atom_suffixes_str[] = { "_alpha", "_beta", "_pre", "_rc", "_/*bogus*/", "_p" };

typedef struct {
	atom_suffixes suffix;
	uint64_t sint;
} atom_suffix;

const char * const atom_op_str[] = { "", ">", ">=", "=", "<=", "<", "~", "!", "!!", "*" };
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

#ifdef _USE_CACHE
static depend_atom *_atom_cache = NULL;
static size_t _atom_cache_len = 0;
#endif

depend_atom *atom_explode(const char *atom);
depend_atom *atom_explode(const char *atom)
{
	depend_atom *ret;
	char *ptr;
	size_t len, slen, idx, sidx;

	/* we allocate mem for atom struct and two strings (strlen(atom)).
	 * the first string is for CAT/PN/PV while the second is for PVR.
	 * PVR needs 3 extra bytes for possible implicit '-r0'. */
	slen = strlen(atom);
	len = sizeof(*ret) + (slen + 1) * sizeof(*atom) * 3 + 3;
#ifdef _USE_CACHE
	if (len <= _atom_cache_len) {
		ret = _atom_cache;
		memset(ret, 0x00, len);
	} else {
		free(_atom_cache);
		_atom_cache = ret = xzalloc(len);
		_atom_cache_len = len;
	}
#else
	ret = xzalloc(len);
#endif
	ptr = (char*)ret;
	ret->P = ptr + sizeof(*ret);
	ret->PVR = ret->P + slen + 1;
	ret->CATEGORY = ret->PVR + slen + 1 + 3;

	/* eat any prefix operators */
	switch (atom[0]) {
	case '>':
		++atom;
		if (atom[0] == '=') {
			++atom;
			ret->pfx_op = ATOM_OP_NEWER_EQUAL;
		} else
			ret->pfx_op = ATOM_OP_NEWER;
		break;
	case '=':
		++atom;
		ret->pfx_op = ATOM_OP_EQUAL;
		break;
	case '<':
		++atom;
		if (atom[0] == '=') {
			++atom;
			ret->pfx_op = ATOM_OP_OLDER_EQUAL;
		} else
			ret->pfx_op = ATOM_OP_OLDER;
		break;
	case '~':
		++atom;
		ret->pfx_op = ATOM_OP_PV_EQUAL;
		break;
	case '!':
		++atom;
		if (atom[0] == '!') {
			++atom;
			ret->pfx_op = ATOM_OP_BLOCK_HARD;
		} else
			ret->pfx_op = ATOM_OP_BLOCK;
		break;
	}
	strcpy(ret->CATEGORY, atom);

	/* eat file name crap */
	if ((ptr = strstr(ret->CATEGORY, ".ebuild")) != NULL)
		*ptr = '\0';

	/* chip off the trailing [::REPO] as needed */
	if ((ptr = strstr(ret->CATEGORY, "::")) != NULL) {
		ret->REPO = ptr + 2;
		*ptr = '\0';
	}

	/* chip off the trailing [:SLOT] as needed */
	if ((ptr = strrchr(ret->CATEGORY, ':')) != NULL) {
		ret->SLOT = ptr + 1;
		*ptr = '\0';
	}

	/* see if we have any suffix operators */
	if ((ptr = strrchr(ret->CATEGORY, '*')) != NULL) {
		/* make sure it's the last byte */
		if (ptr[1] == '\0') {
			ret->sfx_op = ATOM_OP_STAR;
			*ptr = '\0';
		}
	}

	/* break up the CATEOGRY and PVR */
	if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL) {
		ret->PN = ptr + 1;
		*ptr = '\0';
		/* eat extra crap in case it exists */
		if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL)
			ret->CATEGORY = ptr + 1;
	} else {
		ret->PN = ret->CATEGORY;
		ret->CATEGORY = NULL;
	}
	strcpy(ret->PVR, ret->PN);

	/* find -r# */
	ptr = ret->PN + strlen(ret->PN) - 1;
	while (*ptr && ptr > ret->PN) {
		if (!isdigit(*ptr)) {
			if (ptr[0] == 'r' && ptr[-1] == '-') {
				ret->PR_int = atoi(ptr + 1);
				ptr[-1] = '\0';
			} else
				strcat(ret->PVR, "-r0");
			break;
		}
		--ptr;
	}
	strcpy(ret->P, ret->PN);

	/* break out all the suffixes */
	sidx = 0;
	ret->suffixes = xrealloc(ret->suffixes, sizeof(atom_suffix) * (sidx + 1));
	ret->suffixes[sidx].sint = 0;
	ret->suffixes[sidx].suffix = VER_NORM;
	while ((ptr = strrchr(ret->PN, '_')) != NULL) {
		for (idx = 0; idx < ARRAY_SIZE(atom_suffixes_str); ++idx) {
			if (strncmp(ptr, atom_suffixes_str[idx], strlen(atom_suffixes_str[idx])))
				continue;

			/* check this is a real suffix and not _p hitting mod_perl */
			char *tmp_ptr = ptr;
			tmp_ptr += strlen(atom_suffixes_str[idx]);
			ret->suffixes[sidx].sint = atoll(tmp_ptr);
			while (isdigit(*tmp_ptr))
				++tmp_ptr;
			if (*tmp_ptr)
				goto no_more_suffixes;
			ret->suffixes[sidx].suffix = idx;

			++sidx;
			*ptr = '\0';

			ret->suffixes = xrealloc(ret->suffixes, sizeof(atom_suffix) * (sidx + 1));
			ret->suffixes[sidx].sint = 0;
			ret->suffixes[sidx].suffix = VER_NORM;
			break;
		}
		if (*ptr)
			break;
	}
 no_more_suffixes:
	if (sidx)
		--sidx;
	for (idx = 0; idx < sidx; ++idx, --sidx) {
		atom_suffix t = ret->suffixes[sidx];
		ret->suffixes[sidx] = ret->suffixes[idx];
		ret->suffixes[idx] = t;
	}

	/* allow for 1 optional suffix letter */
	ptr = ret->PN + strlen(ret->PN);
	if (ptr[-1] >= 'a' && ptr[-1] <= 'z') {
		ret->letter = ptr[-1];
		--ptr;
	}

	/* eat the trailing version number [-.0-9]+ */
	bool has_pv = false;
	while (--ptr > ret->PN)
		if (*ptr == '-') {
			has_pv = true;
			*ptr = '\0';
			break;
		} else if (*ptr != '.' && !isdigit(*ptr))
			break;
	if (has_pv) {
		ret->PV = ret->P + (ptr - ret->PN) + 1;
	} else {
		/* atom has no version */
		ret->PV = ret->PVR = NULL;
		ret->letter = 0;
	}

	return ret;
}

void atom_implode(depend_atom *atom);
void atom_implode(depend_atom *atom)
{
	if (!atom)
		errf("Atom is empty !");
	free(atom->suffixes);
#ifndef _USE_CACHE
	free(atom);
#endif
}
