/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "atom.h"

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <xalloc.h>

const char * const booga[] = {"!!!", "!=", "==", ">", "<"};

const char * const atom_suffixes_str[] = {
	"_alpha", "_beta", "_pre", "_rc", "_/*bogus*/", "_p"
};

const char * const atom_op_str[] = {
	"", ">", ">=", "=", "<=", "<", "~", "!", "!!", "*"
};

#ifdef EBUG
void
atom_print(const depend_atom *atom)
{
	if (atom->CATEGORY)
		printf("%s/", atom->CATEGORY);
	printf("%s", atom->P);
	if (atom->PR_int)
		printf("-r%i", atom->PR_int);
	if (atom->SLOT)
		printf(":%s", atom->SLOT);
	if (atom->REPO)
		printf("::%s", atom->REPO);
}
#endif

#ifdef _USE_CACHE
static depend_atom *_atom_cache = NULL;
static size_t _atom_cache_len = 0;
#endif

depend_atom *
atom_explode(const char *atom)
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
		switch (atom[0]) {
		case '!':
			++atom;
			ret->pfx_op = ATOM_OP_BLOCK_HARD;
			break;
		case '>':
			++atom;
			if (atom[0] == '=') {
				++atom;
				ret->pfx_op = ATOM_OP_OLDER;
			} else
				ret->pfx_op = ATOM_OP_OLDER_EQUAL;
			break;
		case '<':
			++atom;
			if (atom[0] == '=') {
				++atom;
				ret->pfx_op = ATOM_OP_NEWER;
			} else
				ret->pfx_op = ATOM_OP_NEWER_EQUAL;
			break;
		default:
			ret->pfx_op = ATOM_OP_BLOCK;
			break;
		}
		break;
	}
	strcpy(ret->CATEGORY, atom);

	/* eat file name crap when given an (autocompleted) path */
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

		/* ignore slots that are about package matching */
		if (ret->SLOT[0] == '=' || ret->SLOT[0] == '*')
			ret->SLOT = NULL;
	}

	/* see if we have any suffix operators */
	if ((ptr = strrchr(ret->CATEGORY, '*')) != NULL) {
		/* make sure it's the last byte */
		if (ptr[1] == '\0') {
			ret->sfx_op = ATOM_OP_STAR;
			*ptr = '\0';
		}
	}

	/* break up the CATEGORY and PVR */
	if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL) {
		ret->PN = ptr + 1;
		*ptr = '\0';
		/* eat extra crap in case it exists, this is a feature to allow
		 * /path/to/pkg.ebuild */
		if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL)
			ret->CATEGORY = ptr + 1;
	} else {
		ret->PN = ret->CATEGORY;
		ret->CATEGORY = NULL;
	}

	/* CATEGORY should be all set here, PN contains everything up to
	 * SLOT, REPO or '*'
	 * PN must not end in a hyphen followed by anything matching version
	 * syntax, version syntax starts with a number, so "-[0-9]" is a
	 * separator from PN to PV* -- except it doesn't when the thing
	 * doesn't validate as version :( */

	ptr = ret->PN;
	{
		char *lastpv = NULL;
		char *pv;

		while ((ptr = strchr(ptr, '-')) != NULL) {
			ptr++;
			if (!isdigit(*ptr))
				continue;

			/* so we should have something like "-2" here, see if this
			 * checks out as valid version string */
			pv = ptr;
			while (*++ptr != '\0') {
				if (*ptr != '.' && !isdigit(*ptr))
					break;
			}
			/* allow for 1 optional suffix letter */
			if (*ptr >= 'a' && *ptr <= 'z')
				ret->letter = *ptr++;
			if (*ptr == '_' || *ptr == '\0' || *ptr == '-') {
				lastpv = pv;
				continue;  /* valid, keep searching */
			}
			ret->letter = '\0';
		}
		ptr = lastpv;
	}

	if (ptr == NULL) {
		/* atom has no version, this is it */
		strcpy(ret->P, ret->PN);
		ret->PVR = NULL;
		return ret;
	}
	ret->PV = ptr;

	/* find -r# */
	ptr = ret->PV + strlen(ret->PV) - 1;
	while (*ptr && ptr > ret->PV) {
		if (!isdigit(*ptr)) {
			if (ptr[0] == 'r' && ptr[-1] == '-') {
				ret->PR_int = atoi(ptr + 1);
				ptr[-1] = '\0';
			}
			break;
		}
		ptr--;
	}
	strcpy(ret->P, ret->PN);
	ret->PV[-1] = '\0';

	/* break out all the suffixes */
	sidx = 0;
	ret->suffixes = xrealloc(ret->suffixes, sizeof(atom_suffix) * (sidx + 1));
	ret->suffixes[sidx].sint = 0;
	ret->suffixes[sidx].suffix = VER_NORM;
	ptr = ret->PV + strlen(ret->PV) - 1;
	while (ptr-- > ret->PV) {
		if (*ptr != '_')
			continue;
		for (idx = 0; idx < ARRAY_SIZE(atom_suffixes_str); ++idx) {
			if (strncmp(ptr, atom_suffixes_str[idx],
						strlen(atom_suffixes_str[idx])))
				continue;

			ret->suffixes[sidx].sint =
				atoll(ptr + strlen(atom_suffixes_str[idx]));
			ret->suffixes[sidx].suffix = idx;

			++sidx;

			ret->suffixes = xrealloc(ret->suffixes,
					sizeof(atom_suffix) * (sidx + 1));
			ret->suffixes[sidx].sint = 0;
			ret->suffixes[sidx].suffix = VER_NORM;
			break;
		}
	}
	if (sidx)
		--sidx;
	for (idx = 0; idx < sidx; ++idx, --sidx) {
		atom_suffix t = ret->suffixes[sidx];
		ret->suffixes[sidx] = ret->suffixes[idx];
		ret->suffixes[idx] = t;
	}

	/* size is malloced above with the required space in mind */
	sprintf(ret->PVR, "%s-r%i", ret->PV, ret->PR_int);

	return ret;
}

void
atom_implode(depend_atom *atom)
{
	if (!atom)
		errf("Atom is empty !");
	free(atom->suffixes);
#ifndef _USE_CACHE
	free(atom);
#endif
}

static int
_atom_compare_match(int ret, atom_operator op)
{
	if (op == ATOM_OP_NONE)
		return ret;

#define E(x) ((x) ? EQUAL : NOT_EQUAL)
	switch (op) {
	case ATOM_OP_NEWER:
		return E(ret == NEWER);
	case ATOM_OP_NEWER_EQUAL:
		return E(ret != OLDER);
	case ATOM_OP_PV_EQUAL:	/* we handled this in atom_compare */
	case ATOM_OP_EQUAL:
		return E(ret == EQUAL);
	case ATOM_OP_OLDER_EQUAL:
		return E(ret != NEWER);
	case ATOM_OP_OLDER:
		return E(ret == OLDER);
	default:
		/* blockers/etc... */
		return NOT_EQUAL;
	}
#undef E
}

/* a1 <return value> a2
 * foo-1 <EQUAL> foo-1
 * foo-1 <OLDER> foo-2
 * foo-1 <NOT_EQUAL> bar-1
 */
int
atom_compare(const depend_atom *a1, const depend_atom *a2)
{
	/* sanity check that at most one has operators */
	if (a1->pfx_op != ATOM_OP_NONE || a1->sfx_op != ATOM_OP_NONE) {
		/* this is bogus, so punt it */
		if (a2->pfx_op != ATOM_OP_NONE || a2->sfx_op != ATOM_OP_NONE)
			return NOT_EQUAL;
		/* swap a1 & a2 so that a2 is the atom with operators */
		const depend_atom *as = a2;
		a2 = a1;
		a1 = as;
	}
	atom_operator pfx_op = a2->pfx_op;
	atom_operator sfx_op = a2->sfx_op;

	/* check slot */
	if (a1->SLOT || a2->SLOT) {
		if (!a1->SLOT || !a2->SLOT || strcmp(a1->SLOT, a2->SLOT))
			return NOT_EQUAL;
	}

	/* check repo */
	if (a1->REPO || a2->REPO) {
		if (!a1->REPO || !a2->REPO || strcmp(a1->REPO, a2->REPO))
			return NOT_EQUAL;
	}

	/* Check category, iff both are specified.  This way we can match
	 * atoms like "sys-devel/gcc" and "gcc".
	 */
	if (a1->CATEGORY && a2->CATEGORY) {
		if (strcmp(a1->CATEGORY, a2->CATEGORY))
			return NOT_EQUAL;
	}

	/* check name */
	if (a1->PN && a2->PN) {
		if (strcmp(a1->PN, a2->PN))
			return NOT_EQUAL;
	} else if (a1->PN || a2->PN)
		return NOT_EQUAL;

	/* in order to handle suffix globs, we need to know all of the
	 * version elements provided in it ahead of time
	 */
	unsigned int ver_bits = 0;
	if (sfx_op == ATOM_OP_STAR) {
		if (a2->letter)
			ver_bits |= (1 << 0);
		if (a2->suffixes[0].suffix != VER_NORM)
			ver_bits |= (1 << 1);
		/* This doesn't handle things like foo-1.0-r0*, but that atom
		 * doesn't ever show up in practice, so who cares.
		 */
		if (a2->PR_int)
			ver_bits |= (1 << 2);
	}

	/* check version */
	if (a1->PV && a2->PV) {
		char *s1, *s2;
		uint64_t n1, n2;
		/* first we compare the version [1.0]z_alpha1 */
		s1 = a1->PV;
		s2 = a2->PV;
		while (s1 || s2) {
			if (s1 && s2) {
				/* deal with leading zeros */
				while (*s1 == '0' && *s2 == '0') {
					++s1;
					++s2;
				}
				if (*s1 == '0' && isdigit(*s2))
					return _atom_compare_match(OLDER, pfx_op);
				else if (*s2 == '0' && isdigit(*s1))
					return _atom_compare_match(NEWER, pfx_op);
			} else if (sfx_op == ATOM_OP_STAR && !s2 && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
			n1 = (s1 ? atoll(s1) : 0);
			n2 = (s2 ? atoll(s2) : 0);
			if (n1 < n2)
				return _atom_compare_match(OLDER, pfx_op);
			else if (n1 > n2)
				return _atom_compare_match(NEWER, pfx_op);
			if (s1) {
				s1 = strchr(s1, '.');
				if (s1) ++s1;
			}
			if (s2) {
				s2 = strchr(s2, '.');
				if (s2) ++s2;
			}
		}

		/* compare trailing letter 1.0[z]_alpha1 */
		if (sfx_op == ATOM_OP_STAR) {
			ver_bits >>= 1;
			if (!a2->letter && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
		}
		if (a1->letter < a2->letter)
			return _atom_compare_match(OLDER, pfx_op);
		if (a1->letter > a2->letter)
			return _atom_compare_match(NEWER, pfx_op);
		/* find differing suffixes 1.0z[_alpha1] */
		const atom_suffix *as1 = &a1->suffixes[0];
		const atom_suffix *as2 = &a2->suffixes[0];
		while (as1->suffix == as2->suffix) {
			if (as1->suffix == VER_NORM ||
			    as2->suffix == VER_NORM)
				break;

			if (as1->sint != as2->sint)
				break;

			++as1;
			++as2;
		}
		/* compare suffixes 1.0z[_alpha]1 */
		if (sfx_op == ATOM_OP_STAR) {
			ver_bits >>= 1;
			if (as2->suffix == VER_NORM && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
		}
		if (as1->suffix < as2->suffix)
			return _atom_compare_match(OLDER, pfx_op);
		else if (as1->suffix > as2->suffix)
			return _atom_compare_match(NEWER, pfx_op);
		/* compare suffix number 1.0z_alpha[1] */
		if (sfx_op == ATOM_OP_STAR && !as2->sint && !ver_bits)
			return _atom_compare_match(EQUAL, pfx_op);
		else if (as1->sint < as2->sint)
			return _atom_compare_match(OLDER, pfx_op);
		else if (as1->sint > as2->sint)
			return _atom_compare_match(NEWER, pfx_op);
		/* fall through to -r# check below */
	} else if (a1->PV || a2->PV)
		return EQUAL;

	/* Make sure the -r# is the same. */
	if ((sfx_op == ATOM_OP_STAR && !a2->PR_int) ||
	    pfx_op == ATOM_OP_PV_EQUAL ||
	    a1->PR_int == a2->PR_int)
		return _atom_compare_match(EQUAL, pfx_op);
	else if (a1->PR_int < a2->PR_int)
		return _atom_compare_match(OLDER, pfx_op);
	else
		return _atom_compare_match(NEWER, pfx_op);
}

int
atom_compare_str(const char * const s1, const char * const s2)
{
	depend_atom *a1, *a2;
	int ret = ERROR;

	a1 = atom_explode(s1);
	if (!a1)
		return ret;

	a2 = atom_explode(s2);
	if (!a2)
		goto implode_a1_ret;

	ret = atom_compare(a1, a2);

	atom_implode(a2);
implode_a1_ret:
	atom_implode(a1);
	return ret;
}
