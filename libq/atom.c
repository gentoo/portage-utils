/*
 * Copyright 2005-2022 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "atom.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <xalloc.h>

const char * const atom_suffixes_str[] = {
	"_alpha", "_beta", "_pre", "_rc", "_/*bogus*/", "_p"
};

const char * const atom_slotdep_str[] = {
	"", "=", "*"
};

const char * const atom_usecond_str[] = {
	"", "!", "-", "?", "=", "(+)", "(-)"
};

const char * const atom_blocker_str[] = {
	"", "!", "!!", "^"
};

const char * const atom_op_str[] = {
	"", "=", ">", ">=", "<", "<=", "~", "*"
};

const char * const booga[] = {"!!!", "!=", "==", ">", "<"};

/* split string into individual components, known as an atom
 * for a definition of which variable contains what, see:
 * https://dev.gentoo.org/~ulm/pms/head/pms.html#x1-10800011 */
depend_atom *
atom_explode_cat(const char *atom, const char *cat)
{
	depend_atom *ret;
	char *ptr;
	size_t len;
	size_t slen;
	size_t idx;
	size_t sidx;
	char *lastpv = NULL;
	char *pv;

	/* PMS 11.1 recap:
	 * CAT  The package’s category                    app-editors
	 * PF   Package name, version, and revision (if any)          vim-7.0.174-r1
	 * PVR  Package version and revision (if any)                     7.0.174-r1
	 * P    Package name and version, without the revision part   vim-7.0.174
	 * PV   Package version, with no revision                         7.0.174
	 * PN   Package name                                          vim
	 * PR   Package revision, or r0 if none exists                            r1
	 *
	 * Thus, CAT/PF is the full allocation of the input string, for the
	 * rest (P, PN, PV, PR, PVR) we need copies.  We represent PR as an
	 * integer, which leaves the set to PN, P and PV.
	 * PVR is an offset inside PF, likewise, PV is an offset inside P.
	 * We allocate memory for atom struct, one string for CAT/PF + PVR,
	 * another to cover PN and a final one for P + PV. */
	slen = strlen(atom) + 1;
	len = cat != NULL ? strlen(cat) + 1 : 0;
	ret = xmalloc(sizeof(*ret) + (slen * 3) + len);
	memset(ret, '\0', sizeof(*ret));

	/* assign pointers to the three storage containers */
	ret->CATEGORY = (char *)ret + sizeof(*ret) + len;     /* CAT PF PVR */
	ret->P        = ret->CATEGORY + slen;                 /* P   PV     */
	ret->PN       = ret->P + slen;                        /* PN         */

	/* check for blocker operators */
	ret->blocker = ATOM_BL_NONE;
	if (*atom == '!') {
		ret->blocker++;
		atom++;
		if (*atom == '!') {
			ret->blocker++;
			atom++;
		}
	} else if (*atom == '^') {
		ret->blocker = ATOM_BL_ANTISLOT;
		atom++;
	}

	/* eat any prefix operators */
	ret->pfx_op = ATOM_OP_NONE;
	switch (*atom) {
	case '>':
		ret->pfx_op = ATOM_OP_NEWER;
		atom++;
		break;
	case '<':
		ret->pfx_op = ATOM_OP_OLDER;
		atom++;
		break;
	case '~':
		ret->pfx_op = ATOM_OP_PV_EQUAL;
		atom++;
		break;
	}
	if (*atom == '=') {
		ret->pfx_op += ATOM_OP_EQUAL;
		atom++;
	}

	/* fill in full block */
	strcpy(ret->CATEGORY, atom);

	/* eat file name crap when given an (autocompleted) path */
	if ((ptr = strstr(ret->CATEGORY, ".ebuild")) != NULL ||
			(ptr = strstr(ret->CATEGORY, ".tbz2")) != NULL)
		*ptr = '\0';

	/* chip off the trailing ::REPO as needed */
	if ((ptr = strstr(ret->CATEGORY, "::")) != NULL) {
		ret->REPO = ptr + 2;
		*ptr = '\0';
		/* set to NULL if there's nothing */
		if (ret->REPO[0] == '\0')
			ret->REPO = NULL;
	}

	/* hunt down build with USE dependencies */
	if ((ptr = strrchr(ret->CATEGORY, ']')) != NULL && ptr[1] == '\0' &&
			(ptr = strrchr(ret->CATEGORY, '[')) != NULL)
	{
		atom_usedep *w = NULL;
		do {
			if (ret->usedeps == NULL) {
				ret->usedeps = w = xmalloc(sizeof(atom_usedep));
			} else {
				w = w->next = xmalloc(sizeof(atom_usedep));
			}
			w->next = NULL;
			*ptr++ = '\0';
			w->pfx_cond = w->sfx_cond = ATOM_UC_NONE;
			switch (*ptr) {
				case '-':
					w->pfx_cond = ATOM_UC_NEG;
					ptr++;
					break;
				case '!':
					w->pfx_cond = ATOM_UC_NOT;
					ptr++;
					break;
			}
			w->use = ptr;
			while (*ptr != '\0') {
				switch (*ptr) {
					case '?':
						w->sfx_cond = ATOM_UC_COND;
						*ptr++ = '\0';
						break;
					case '=':
						w->sfx_cond = ATOM_UC_EQUAL;
						*ptr++ = '\0';
						break;
					case '(':
						if (strncmp(ptr, "(+)", 3) == 0) {
							w->sfx_cond = ATOM_UC_PREV_ENABLED;
							*ptr = '\0';
							ptr += 3;
						} else if (strncmp(ptr, "(-)", 3) == 0) {
							w->sfx_cond = ATOM_UC_PREV_ENABLED;
							*ptr = '\0';
							ptr += 3;
						} else {
							ptr++;
						}
						break;
					case ',':
					case ']':
						*ptr = ']';
						break;
					default:
						ptr++;
				}
				if (*ptr == ']')
					break;
			}
		} while (ptr[1] != '\0');
		*ptr++ = '\0';
	}

	/* chip off the trailing :SLOT as needed */
	if ((ptr = strrchr(ret->CATEGORY, ':')) != NULL) {
		*ptr++ = '\0';
		ret->SLOT = ptr;

		/* deal with slot operators */
		if ((ptr = strrchr(ret->SLOT, '=')) != NULL && ptr[1] == '\0') {
			ret->slotdep = ATOM_SD_ANY_REBUILD;
			*ptr = '\0';
		}
		if ((ptr = strrchr(ret->SLOT, '*')) != NULL && ptr[1] == '\0') {
			ret->slotdep = ATOM_SD_ANY_IGNORE;
			*ptr = '\0';
		}

		/* cut in two when sub-slot */
		if ((ptr = strchr(ret->SLOT, '/')) != NULL) {
			*ptr++ = '\0';
			if (*ptr != '\0')
				ret->SUBSLOT = ptr;
		}

		/* set to NULL if there's nothing */
		if (ret->SLOT[0] == '\0')
			ret->SLOT = NULL;

		/* PMS 7.2: SUBSLOT defaults to SLOT when unset */
		if (ret->SUBSLOT == NULL)
			ret->SUBSLOT = ret->SLOT;
	}

	/* see if we have any suffix operators */
	if ((ptr = strrchr(ret->CATEGORY, '*')) != NULL && ptr[1] == '\0') {
		ret->sfx_op = ATOM_OP_STAR;
		*ptr = '\0';
	}

	/* break up the CATEGORY, PF and PVR */
	if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL) {
		*ptr++ = '\0';
		ret->PF = ptr;

		/* set PN to NULL if there's nothing */
		if (ret->PF[0] == '\0')
			ret->PF = NULL;

		/* eat extra crap in case it exists, this is a feature to allow
		 * /path/to/pkg.ebuild, doesn't work with prefix operators
		 * though */
		if ((ptr = strrchr(ret->CATEGORY, '/')) != NULL)
			ret->CATEGORY = ptr + 1;
	} else {
		ret->PF = ret->CATEGORY;
		ret->CATEGORY = NULL;
	}

	/* inject separate CATEGORY when given, this will override any found
	 * CATEGORY, which is what it could be used for too */
	if (cat != NULL) {
		ret->CATEGORY = (char *)ret + sizeof(*ret);
		memcpy(ret->CATEGORY, cat, len);
	}

	if (ret->PF == NULL) {
		/* atom has no name, this is it */
		ret->P = NULL;
		ret->PN = NULL;
		ret->PVR = NULL;

		return ret;
	}

	/* CATEGORY should be all set here, PF contains everything up to
	 * SLOT, REPO or '*'
	 * PMS 3.1.2 says PN must not end in a hyphen followed by
	 * anything matching version syntax.  PMS 3.2 version syntax
	 * starts with a number, so "-[0-9]" is a separator from PN to
	 * PV* -- except it doesn't when the thing doesn't validate as
	 * version :( */

	ptr = ret->PF;
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
		if (*ptr == '_' || *ptr == '-' || *ptr == '\0') {
			lastpv = pv;
			continue;  /* valid, keep searching */
		}
		ret->letter = '\0';
	}
	ptr = lastpv;

	if (ptr == NULL) {
		/* atom has no version, this is it */
		ret->P = ret->PN = ret->PF;
		ret->PV = ret->PVR = NULL;

		return ret;
	}

	ret->PVR = ptr;
	snprintf(ret->PN, slen, "%.*s", (int)(ret->PVR - 1 - ret->PF), ret->PF);

	/* find -r# */
	pv = NULL;
	ptr = ret->PVR + strlen(ret->PVR) - 1;
	while (*ptr && ptr > ret->PVR) {
		if (!isdigit((int)*ptr)) {
			if (ptr[0] == 'r' && ptr[-1] == '-') {
				ret->PR_int = atoi(ptr + 1);
				pv = &ptr[-1];
			}
			break;
		}
		ptr--;
	}
	if (pv != NULL) {
		snprintf(ret->P, slen, "%.*s", (int)(pv - ret->PF), ret->PF);
	} else {
		ret->P = ret->PF;
	}
	ret->PV = ret->P + (ret->PVR - ret->PF);

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

	return ret;
}

depend_atom *
atom_clone(depend_atom *atom)
{
	depend_atom *ret;
	char *p;
	size_t alen;
	size_t clen = 0;
	size_t flen = 0;
	size_t plen = 0;
	size_t nlen = 0;
	size_t slen = 0;
	size_t sslen = 0;
	size_t rlen = 0;

	if (atom->REPO != NULL)
		rlen = strlen(atom->REPO) + 1;
	if (atom->SLOT != NULL)
		slen = strlen(atom->SLOT) + 1;
	if (atom->SUBSLOT != NULL && atom->SUBSLOT != atom->SLOT)
		sslen = strlen(atom->SUBSLOT) + 1;
	if (atom->CATEGORY != NULL)
		clen = strlen(atom->CATEGORY) + 1;
	if (atom->PF != NULL)
		flen = strlen(atom->PF) + 1;  /* should include PVR */
	if (atom->P != NULL)
		plen = strlen(atom->P) + 1;  /* should include PV */
	if (atom->PN != NULL)
		nlen = strlen(atom->PN) + 1;

	alen = sizeof(*ret) + clen + flen + plen + nlen + rlen + slen + sslen;
	ret = xmalloc(alen);
	memset(ret, '\0', sizeof(*ret));

	/* build up main storage pointers, see explode */
	p = (char *)ret + sizeof(*ret);
	if (atom->CATEGORY != NULL) {
		ret->CATEGORY = p;
		memcpy(ret->CATEGORY, atom->CATEGORY, clen);
		p += clen;
	}
	if (atom->PF != NULL) {
		ret->PF = p;
		memcpy(ret->PF, atom->PF, flen);
		p += flen;
	}
	if (atom->PVR > atom->PF && atom->PVR < (atom->PF + flen))
		ret->PVR = ret->PF + (atom->PVR - atom->PF);
	if (atom->P != NULL) {
		ret->P = p;
		memcpy(ret->P, atom->P, plen);
		p += plen;
	}
	if (atom->PV > atom->P && atom->PV < (atom->P + plen))
		ret->PV = ret->P + (atom->PV - atom->P);
	if (atom->PN != NULL) {
		ret->PN = p;
		memcpy(ret->PN, atom->PN, nlen);
		p += nlen;
	}
	if (atom->SLOT != NULL) {
		ret->SLOT = p;
		memcpy(ret->SLOT, atom->SLOT, slen);
		p += slen;
	}
	if (atom->SUBSLOT != NULL) {
		if (atom->SUBSLOT == atom->SLOT) {  /* PMS 7.2 */
			ret->SUBSLOT = ret->SLOT;
		} else {
			ret->SUBSLOT = p;
			memcpy(ret->SUBSLOT, atom->SUBSLOT, sslen);
			p += sslen;
		}
	}
	if (atom->REPO != NULL) {
		ret->REPO = p;
		memcpy(ret->REPO, atom->REPO, rlen);
		p += rlen;
	}

	ret->blocker = atom->blocker;
	ret->pfx_op = atom->pfx_op;
	ret->sfx_op = atom->pfx_op;
	ret->PR_int = atom->PR_int;
	ret->letter = atom->letter;
	ret->slotdep = atom->slotdep;

	if (atom->suffixes != NULL) {
		for (slen = 0; atom->suffixes[slen].suffix != VER_NORM; slen++)
			;
		slen++;
		ret->suffixes = xmalloc(sizeof(ret->suffixes[0]) * slen);
		memcpy(ret->suffixes, atom->suffixes, sizeof(ret->suffixes[0]) * slen);
	}

	if (atom->usedeps) {
		atom_usedep *w;
		atom_usedep *n = NULL;

		for (w = atom->usedeps; w != NULL; w = w->next) {
			nlen = w->use != NULL ? strlen(w->use) + 1 : 0;
			if (n == NULL) {
				atom->usedeps = n = xmalloc(sizeof(*n) + nlen);
			} else {
				n = n->next = xmalloc(sizeof(*n) + nlen);
			}
			n->next = NULL;
			n->pfx_cond = w->pfx_cond;
			n->sfx_cond = w->sfx_cond;
			n->use = (char *)n + sizeof(*n);
			memcpy(n->use, w->use, nlen);
		}
	}

	return ret;
}

void
atom_implode(depend_atom *atom)
{
	if (!atom)
		errf("Atom is empty !");
	while (atom->usedeps != NULL) {
		atom_usedep *n = atom->usedeps->next;
		free(atom->usedeps);
		atom->usedeps = n;
	}
	free(atom->suffixes);
	free(atom);
}

static atom_equality
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
atom_equality
atom_compare_flg(const depend_atom *data, const depend_atom *query, int flags)
{
	atom_operator pfx_op;
	atom_operator sfx_op;
	atom_blocker bl_op;
	unsigned int ver_bits;

	/* remember:
	 * query should have operators, if data has them, they are ignored */

	/* here comes the antislot: bug #683430
	 * it basically is a feature to select versions that are *not*
	 * what's queried for, but requiring SLOT to be set
	 *
	 * recap of slot operators:
	 *
	 * DEPEND  perl:=        (any slot change, rebuild)
	 *         perl:0=       (any sub-slot change, rebuild)
	 *         perl:*        (any slot will do, never rebuild)
	 *         perl:0*       (any sub-slot will do, never rebuild ?valid?)
	 *         perl:0        (effectively we can treat * as absent)
	 *
	 * VDB     perl:0/5.28=  (the slot/subslot it satisfied when merging)
	 *
	 * ebuild  perl:0/5.26
	 *         perl:0        (SLOT defaults to 0)
	 *
	 * query   perl:0        (matches perl:0, perl:0/5.28)
	 *         perl:0/5.28   (matches any perl:0/5.28)
	 *        !perl:0/5.28   (matches perl, perl:0, perl:0/5.26, perl:1)
	 *        ^perl:0/5.28   (matches perl:0/5.26, perl:0/5.30)
	 *        ^perl:0        (matches perl:1)
	 *         perl:=        (= in this case is meaningless: perl, perl:0 ...)
	 *  (with ^ being a portage-utils addition to match antislot)
	 */
	bl_op = query->blocker;
	if (bl_op == ATOM_BL_ANTISLOT) {
		/* just disable/ignore antislot op when SLOT is supposed to be
		 * ignored */
		if (!(flags & ATOM_COMP_NOSLOT)) {
			/* ^perl -> match anything with a SLOT */
			if (query->SLOT == NULL && data->SLOT == NULL)
				return NOT_EQUAL;
			if (query->SLOT != NULL) {
				if (query->SUBSLOT == query->SLOT ||
						flags & ATOM_COMP_NOSUBSLOT)
				{
					/* ^perl:0 -> match different SLOT */
					if (data->SLOT == NULL ||
							strcmp(query->SLOT, data->SLOT) == 0)
						return NOT_EQUAL;
				} else {
					/* ^perl:0/5.28 -> match SLOT, but different SUBSLOT */
					if (data->SLOT == NULL ||
							strcmp(query->SLOT, data->SLOT) != 0)
						return NOT_EQUAL;
					if (!(flags & ATOM_COMP_NOSUBSLOT))
						if (data->SUBSLOT == query->SLOT ||
								strcmp(query->SUBSLOT, data->SUBSLOT) == 0)
							return NOT_EQUAL;
				}
			}
		}
		bl_op = ATOM_BL_NONE;  /* ease work below */
	} else if (query->SLOT != NULL && !(flags & ATOM_COMP_NOSLOT)) {
		/* check SLOT only when query side has it */
		if (data->SLOT == NULL) {
			if (bl_op == ATOM_BL_NONE)
				return NOT_EQUAL;
		} else {
			if (strcmp(query->SLOT, data->SLOT) != 0) {
				/* slot has differs */
				if (bl_op == ATOM_BL_NONE)
					return NOT_EQUAL;
			} else if (!(flags & ATOM_COMP_NOSUBSLOT)) {
				if (query->SUBSLOT != query->SLOT) {
					if (data->SUBSLOT == data->SLOT) {
						if (bl_op == ATOM_BL_NONE)
							return NOT_EQUAL;
					} else {
						if (strcmp(query->SUBSLOT, data->SUBSLOT) != 0) {
							if (bl_op == ATOM_BL_NONE)
								return NOT_EQUAL;
						}
					}
				}
			}
		}
	}

	/* handle the inversing effect of blockers */
	pfx_op = query->pfx_op;
	sfx_op = query->sfx_op;
	if (bl_op != ATOM_BL_NONE) {
		switch (pfx_op) {
			case ATOM_OP_NEWER:
				pfx_op = ATOM_OP_OLDER_EQUAL;
				break;
			case ATOM_OP_NEWER_EQUAL:
				pfx_op = ATOM_OP_OLDER;
				break;
			case ATOM_OP_OLDER:
				pfx_op = ATOM_OP_NEWER_EQUAL;
				break;
			case ATOM_OP_OLDER_EQUAL:
				pfx_op = ATOM_OP_NEWER;
				break;
			case ATOM_OP_EQUAL:
			case ATOM_OP_PV_EQUAL:
			default:
				pfx_op = ATOM_OP_NEQUAL;
				break;
		}
	}

	/* check REPO, if query has it, ignore blocker stuff for this one */
	if (query->REPO != NULL && !(flags & ATOM_COMP_NOREPO)) {
		if (data->REPO == NULL)
			return NOT_EQUAL;
		if (strcmp(query->REPO, data->REPO) != 0)
			return NOT_EQUAL;
	}

	/* check CATEGORY, if query has it, so we match
	 * atoms like "sys-devel/gcc" and "gcc" */
	if (query->CATEGORY != NULL) {
		if (data->CATEGORY == NULL)
			return NOT_EQUAL;
		if (strcmp(query->CATEGORY, data->CATEGORY) != 0) {
			if (bl_op == ATOM_BL_NONE)
				return NOT_EQUAL;
		} else {
			if (bl_op != ATOM_BL_NONE && query->PN == NULL)
				return EQUAL;
		}
	}

	/* check PN, this may be absent if query is for CATEGORY only */
	if (query->PN != NULL) {
		if (data->PN == NULL)
			return NOT_EQUAL;
		if (strcmp(query->PN, data->PN) != 0) {
			if (bl_op == ATOM_BL_NONE)
				return NOT_EQUAL;
		} else {
			if (bl_op != ATOM_BL_NONE && query->PV == NULL)
				return EQUAL;
		}
	}

	/* in order to handle suffix globs, we need to know all of the
	 * version elements provided in it ahead of time */
	ver_bits = 0;
	if (sfx_op == ATOM_OP_STAR) {
		if (query->letter)
			ver_bits |= (1 << 0);
		if (query->suffixes && query->suffixes[0].suffix != VER_NORM)
			ver_bits |= (1 << 1);
		/* This doesn't handle things like foo-1.0-r0*, but that atom
		 * doesn't ever show up in practice, so who cares. */
		if (query->PR_int)
			ver_bits |= (1 << 2);
	}

	/* check version */
	if (data->PV && query->PV) {
		char              *s1;
		char              *ends1;
		char              *s2;
		char              *ends2;
		long long          n1;
		long long          n2;
		const atom_suffix *as1;
		const atom_suffix *as2;

		/* PMS 3.3 Version Comparison
		 *
		 * Algorithm 3.1: Version comparison top-level logic
		 * 1:  let A and B be the versions to be compared
		 * 2:  compare numeric components using Algorithm 3.2
		 * 3:  compare letter components using Algorithm 3.4
		 * 4:  compare suffixes using Algorithm 3.5
		 * 5:  compare revision components using Algorithm 3.7
		 * 6:  return  A = B
		 */

		/* step 2: numeric components
		 *
		 * Algorithm 3.2: Version comparison logic for numeric
		 * components
		 *  1:  define the notations Ank and Bnk to mean the kth numeric
		 *      component of A and B respectively, using 0-based indexing
		 *  2:  if An0 > Bn0 using integer comparison then
		 *  3:    return  A > B
		 *  4:  else if An0 < Bn0 using integer comparison then
		 *  5:    return  A < B
		 *  6:  end if
		 *  7:  let Ann be the number of numeric components of A
		 *  8:  let Bnn be the number of numeric components of B
		 *  9:  for all i such that i ≥ 1 and i < Ann and i < Bnn, in
		 *      ascending order do
		 * 10:    compare Ani and Bni using Algorithm 3.3
		 * 11:  end for
		 * 12:  if Ann > Bnn then
		 * 13:    return  A > B
		 * 14:  else if Ann < Bnn then
		 * 15:    return  A < B
		 * 16:  end if
		 *
		 * Algorithm 3.3: Version comparison logic for each numeric
		 * component after the first
		 *  1:  if either Ani or Bni has a leading 0 then
		 *  2:    let An′i be Ani with any trailing 0s removed
		 *  3:    let Bn′i be Bni with any trailing 0s removed
		 *  4:    if An′i > Bn′i using ASCII stringwise comparison then
		 *  5:      return  A > B
		 *  6:    else if An′i < Bn′i using ASCII stringwise comparison then
		 *  7:      return  A < B
		 *  8:    end if
		 *  9:  else
		 * 10:    if Ani > Bni using integer comparison then
		 * 11:      return  A > B
		 * 12:    else if Ani < Bni using integer comparison then
		 * 13:      return  A < B
		 * 14:    end if
		 * 15:  end if
		 */
		s1    = data->PV;   /* A */
		s2    = query->PV;  /* B */
		ends1 = NULL;
		ends2 = NULL;
		n1    = 0;
		n2    = 0;
		while (s1 != NULL || s2 != NULL) {
			if (s1 != NULL && s2 != NULL) {
				if (ends1 == NULL) {
					/* 3.2#L2-6: first component integer comparison */
					n1 = strtoll(s1, &ends1, 10);
					if (ends1 == s1)
						n1 = -1;
					n2 = strtoll(s2, &ends2, 10);
					if (ends2 == s2)
						n2 = -1;
				} else {
					/* 3.2#L9-11: run algorithm 3.3 for remaining
					 * components */

					/* 3.3#L1-9: if a leading zero is present do strcmp */
					if (*s1 == '0' || *s2 == '0') {  /* 3.3#L1 */
						/* find end of component */
						for (ends1 = s1;
							 *ends1 != '\0' &&
							 *ends1 != '.' &&
							 *ends1 != '_';
							 ends1++)
							;
						for (ends2 = s2;
							 *ends2 != '\0' &&
							 *ends2 != '.' &&
							 *ends2 != '_';
							 ends2++)
							;
						/* 3.3L2-3: remove *trailing* zeros */
						for (ends1--; ends1 > s1 && *ends1 == '0'; ends1--)
							;
						for (ends2--; ends2 > s2 && *ends2 == '0'; ends2--)
							;
						/* 3.3L4 ASCII stringwise comparison */
						n1 = ends1 - s1;
						n2 = ends2 - s2;
						n1 = strncmp(s1, s2, n1 > n2 ? n1 : n2);
						n2 = 0;
					} else {  /* 3.3#L9 */
						n1 = strtoll(s1, &ends1, 10);
						if (ends1 == s1)
							n1 = -1;
						n2 = strtoll(s2, &ends2, 10);
						if (ends2 == s2)
							n2 = -1;
					}
				}
			} else if (sfx_op == ATOM_OP_STAR && s2 == NULL && !ver_bits) {
				return _atom_compare_match(EQUAL, pfx_op);
			} else {  /* 3.2#L12-16 */
				if (s1 == NULL) {
					n1    = -1;
					n2    =  0;
					ends1 =  NULL;
				}
				else if (s2 == NULL) {
					n1    =  0;
					n2    = -1;
					ends2 =  NULL;
				}
			}

			if (n1 < n2)
				return _atom_compare_match(OLDER, pfx_op);
			else if (n1 > n2)
				return _atom_compare_match(NEWER, pfx_op);

			s1 = *ends1 == '\0' ? NULL : ends1;
			if (s1 != NULL) {
				if (*s1 != '.')
					s1 = strchr(s1, '.');
				if (s1 != NULL)
					s1++;
			}
			s2 = *ends2 == '\0' ? NULL : ends2;
			if (s2 != NULL) {
				if (*s2 != '.')
					s2 = strchr(s2, '.');
				if (s2 != NULL)
					s2++;
			}
		}

		/* step 3: compare trailing letter 1.0[z]_alpha1
		 *
		 * Algorithm 3.4: Version comparison logic for letter components
		 * 1:  let Al be the letter component of A if any, otherwise the
		 *     empty string
		 * 2:  let Bl be the letter component of B if any, otherwise the
		 *     empty string
		 * 3:  if Al > Bl using ASCII stringwise comparison then
		 * 4:    return  A > B
		 * 5:  else if Al < Bl using ASCII stringwise comparison then
		 * 6:    return  A < B
		 * 7:  end if
		 */
		if (sfx_op == ATOM_OP_STAR) {
			ver_bits >>= 1;
			if (!query->letter && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
		}
		if (data->letter < query->letter)
			return _atom_compare_match(OLDER, pfx_op);
		if (data->letter > query->letter)
			return _atom_compare_match(NEWER, pfx_op);

		/* Algorithm 3.5: Version comparison logic for suffixes
		 *  1:  define the notations Ask and Bsk to mean the kth suffix
		 *      of A and B respectively, using 0-based indexing
		 *  2:  let Asn be the number of suffixes of A
		 *  3:  let Bsn be the number of suffixes of B
		 *  4:  for all i such that i ≥ 0 and i < Asn and i < Bsn, in
		 *      ascending order do
		 *  5:    compare Asi and Bsi using algorithm 3.6
		 *  6:  end for
		 *  7:  if Asn > Bsn then
		 *  8:    if AsBsn is of type _p then
		 *  9:      return  A > B
		 * 10:    else
		 * 11:      return  A < B
		 * 12:    end if
		 * 13:  else if Asn < Bsn then
		 * 14:    if BsAsn is of type _p then
		 * 15:      return  A < B
		 * 16:    else
		 * 17:      return  A > B
		 * 18:    end if
		 * 19:  end if
		 *
		 * Algorithm 3.6: Version comparison logic for each suffix
		 *  1:  if Asi and Bsi are of the same type (_alpha vs _beta etc) then
		 *  2:    let As′i be the integer part of Asi if any, otherwise 0
		 *  3:    let Bs′i be the integer part of Bsi if any, otherwise 0
		 *  4:    if As′i > Bs′i, using integer comparison then
		 *  5:      return  A > B
		 *  6:    else if As′i < Bs′i, using integer comparison then
		 *  7:      return  A < B
		 *  8:    end if
		 *  9:  else if the type of Asi is greater than the type of Bsi
		 *      using the ordering _alpha < _beta < _pre < _rc < _p then
		 * 10:    return  A > B
		 * 11:  else
		 * 12:    return  A < B
		 * 13:  end if
		 */

		/* step 4: find differing suffixes 1.0z[_alpha1] */
		as1 = &data->suffixes[0];
		as2 = &query->suffixes[0];
		while (as1->suffix == as2->suffix) {
			if (as1->suffix == VER_NORM || as2->suffix == VER_NORM)
				break;

			if (as1->sint != as2->sint)
				break;

			as1++;
			as2++;
		}

		/* compare suffixes 1.0z[_alpha]1 */
		if (sfx_op == ATOM_OP_STAR) {
			ver_bits >>= 1;
			if (as2->suffix == VER_NORM && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
		}
		if (as1->suffix < as2->suffix)  /* 3.6#L9 */
			return _atom_compare_match(OLDER, pfx_op);
		else if (as1->suffix > as2->suffix)
			return _atom_compare_match(NEWER, pfx_op);
		/* compare suffix number 1.0z_alpha[1] 3.6#L4 */
		if (sfx_op == ATOM_OP_STAR && !as2->sint && !ver_bits)
			return _atom_compare_match(EQUAL, pfx_op);
		else if (as1->sint < as2->sint)
			return _atom_compare_match(OLDER, pfx_op);
		else if (as1->sint > as2->sint)
			return _atom_compare_match(NEWER, pfx_op);
		/* fall through to -r# check below */
	} else if (data->PV || query->PV)
		return EQUAL;

	/* Algorithm 3.7: Version comparison logic for revision components
	 * 1:  let Ar be the integer part of the revision component of A if
	 *     any, otherwise 0
	 * 2:  let Br be the integer part of the revision component of B if
	 *     any, otherwise 0
	 * 3:  if Ar > Br using integer comparison then
	 * 4:    return  A > B
	 * 5:  else if Ar < Br using integer comparison then
	 * 6:    return  A < B
	 * 7:  end if
	 */
	/* first handle wildcarding cases */
	if ((sfx_op == ATOM_OP_STAR && query->PR_int == 0) ||
	    pfx_op == ATOM_OP_PV_EQUAL ||
		flags & ATOM_COMP_NOREV)
		return _atom_compare_match(EQUAL, pfx_op);
	/* Make sure the -r# is the same. 3.7 */
	if (data->PR_int < query->PR_int)
		return _atom_compare_match(OLDER, pfx_op);
	else if (data->PR_int > query->PR_int)
		return _atom_compare_match(NEWER, pfx_op);

	/* binpkg-multi-instance support */
	if (data->BUILDID < query->BUILDID)
		return _atom_compare_match(OLDER, pfx_op);
	if (data->BUILDID > query->BUILDID)
		return _atom_compare_match(NEWER, pfx_op);

	return _atom_compare_match(EQUAL, pfx_op);
}

atom_equality
atom_compare_str(const char * const s1, const char * const s2)
{
	depend_atom *a1, *a2;
	atom_equality ret = ERROR;

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

/**
 * Reconstructs an atom exactly like it was originally given (exploded).
 */
char *
atom_to_string_r(char *buf, size_t buflen, depend_atom *a)
{
	size_t off = 0;
	atom_usedep *ud;

	off += snprintf(buf + off, buflen - off, "%s%s",
			atom_blocker_str[a->blocker], atom_op_str[a->pfx_op]);
	if (a->CATEGORY != NULL)
		off += snprintf(buf + off, buflen - off, "%s/", a->CATEGORY);
	if (a->PN != NULL)
		off += snprintf(buf + off, buflen - off, "%s", a->PN);
	if (a->PV != NULL)
		off += snprintf(buf + off, buflen - off, "-%s", a->PV);
	if (a->PR_int > 0)
		off += snprintf(buf + off, buflen - off, "-r%d", a->PR_int);
	off += snprintf(buf + off, buflen - off, "%s", atom_op_str[a->sfx_op]);
	if (a->SLOT != NULL || a->slotdep != ATOM_SD_NONE)
		off += snprintf(buf + off, buflen - off, ":%s%s%s%s",
				a->SLOT ? a->SLOT : "",
				a->SUBSLOT && a->SUBSLOT != a->SLOT ?  "/" : "",
				a->SUBSLOT && a->SUBSLOT != a->SLOT ? a->SUBSLOT : "",
				atom_slotdep_str[a->slotdep]);
	for (ud = a->usedeps; ud != NULL; ud = ud->next)
		off += snprintf(buf + off, buflen - off, "%s%s%s%s%s",
				ud == a->usedeps ? "[" : "",
				atom_usecond_str[ud->pfx_cond],
				ud->use,
				atom_usecond_str[ud->sfx_cond],
				ud->next == NULL ? "]" : ",");
	if (a->REPO != NULL)
		off += snprintf(buf + off, buflen - off, "::%s", a->REPO);

	return buf;
}

/**
 * Run printf on an atom.  The format field takes the form:
 *  %{keyword}: Always display the field that matches "keyword" or <unset>
 *  %[keyword]: Only display the field when it's set
 * The possible "keywords" are:
 *  CATEGORY  P  PN  PV  PVR  PF  PR  SLOT  SUBSLOT  REPO  USE
 *    - these are all the standard portage variables (so see ebuild(5))
 *    - any prefix of these (e.g. CAT, CA, C) will match as well
 *  pfx - the version qualifier if set (e.g. > < = !)
 *  sfx - the version qualifier if set (e.g. *)
 */
char *
atom_format_r(
		char *buf,
		size_t buflen,
		const char *format,
		const depend_atom *atom)
{
	char bracket;
	const char *fmt;
	const char *p;
	size_t len;
	bool showit;
	bool connected;
	char *ret;

	if (!atom) {
		snprintf(buf, buflen, "%s", "(NULL:atom)");
		return buf;
	}

#define append_buf(B,L,FMT,...) \
	{ \
		len = snprintf(B, L, FMT, __VA_ARGS__); \
		L -= len; \
		B += len; \
	}
	ret = buf;
	p = format;
	while (*p != '\0') {
		fmt = strchr(p, '%');
		if (fmt == NULL) {
			append_buf(buf, buflen, "%s", p);
			return ret;
		} else if (fmt != p) {
			append_buf(buf, buflen, "%.*s", (int)(fmt - p), p);
			connected = false;
		} else {
			connected = true;
		}

		bracket = fmt[1];
		if (bracket == '{' || bracket == '[') {
			connected &= bracket == '[';
			fmt += 2;
			if ((p = strchr(fmt, bracket == '{' ? '}' : ']')) != NULL) {
				len = p - fmt;
				showit = bracket == '{';
#define HN(X) (X ? X : "<unset>")
				if (!strncmp("CATEGORY", fmt, len)) {
					connected = (p[1] == '%') & (bracket == '[');
					if (showit || atom->CATEGORY)
						append_buf(buf, buflen, "%s%s%s%s",
								BOLD, HN(atom->CATEGORY),
								connected ? "/" : "", NORM);
				} else if (!strncmp("P", fmt, len)) {
					if (showit || atom->P)
						append_buf(buf, buflen, "%s%s%s",
								BLUE, HN(atom->P), NORM);
				} else if (!strncmp("PN", fmt, len)) {
					if (showit || atom->PN)
						append_buf(buf, buflen, "%s%s%s",
								BLUE, HN(atom->PN), NORM);
				} else if (!strncmp("PV", fmt, len)) {
					if (showit || atom->PV)
						append_buf(buf, buflen, "%s%s%s",
								CYAN, HN(atom->PV), NORM);
				} else if (!strncmp("PVR", fmt, len)) {
					if (showit || atom->PVR)
						append_buf(buf, buflen, "%s%s%s",
								CYAN, HN(atom->PVR), NORM);
				} else if (!strncmp("PF", fmt, len)) {
					append_buf(buf, buflen, "%s%s%s", BLUE, atom->PN, NORM);
					if (atom->PV)
						append_buf(buf, buflen, "%s-%s%s",
								CYAN, atom->PVR, NORM);
				} else if (!strncmp("PR", fmt, len)) {
					if (showit || atom->PR_int)
						append_buf(buf, buflen, "%sr%d%s",
								CYAN, atom->PR_int, NORM);
				} else if (!strncmp("SLOT", fmt, len)) {
					if (showit || atom->SLOT)
						append_buf(buf, buflen, "%s%s%s%s",
								YELLOW,
								connected ? ":" : "",
								HN(atom->SLOT),
								NORM);
				} else if (!strncmp("SUBSLOT", fmt, len)) {
					if (showit ||
							(atom->SUBSLOT && atom->SUBSLOT != atom->SLOT))
						append_buf(buf, buflen, "%s%s%s%s%s",
								YELLOW,
								connected ? "/" : "",
								HN(atom->SUBSLOT),
								atom_slotdep_str[atom->slotdep],
								NORM);
				} else if (!strncmp("REPO", fmt, len)) {
					if (showit || atom->REPO)
						append_buf(buf, buflen, "%s%s%s%s",
								GREEN, connected ? "::" : "",
								HN(atom->REPO), NORM);
				} else if (!strncmp("pfx", fmt, len)) {
					if (showit || atom->pfx_op != ATOM_OP_NONE)
						append_buf(buf, buflen, "%s",
								atom->pfx_op == ATOM_OP_NONE ?
								"<unset>" : atom_op_str[atom->pfx_op]);
				} else if (!strncmp("sfx", fmt, len)) {
					if (showit || atom->sfx_op != ATOM_OP_NONE)
						append_buf(buf, buflen, "%s",
								atom->sfx_op == ATOM_OP_NONE ?
								"<unset>" : atom_op_str[atom->sfx_op]);
				} else if (!strncmp("USE", fmt, len)) {
					if (showit || atom->usedeps) {
						atom_usedep *ud;
						if (atom->usedeps == NULL) {
							append_buf(buf, buflen, "%s", "<unset>");
						} else {
							if (connected)
								append_buf(buf, buflen, "%s", "[");
							for (ud = atom->usedeps; ud != NULL; ud = ud->next)
								append_buf(buf, buflen, "%s%s%s%s%s%s",
										MAGENTA, atom_usecond_str[ud->pfx_cond],
										ud->use, atom_usecond_str[ud->sfx_cond],
										NORM, ud->next == NULL ? "" :
										(connected ? "," : " "));
							if (connected)
								append_buf(buf, buflen, "%s", "]");
						}
					}
				} else
					append_buf(buf, buflen, "<BAD:%.*s>", (int)len, fmt);
				p++;
#undef HN
			} else {
				p = fmt + 1;
			}
		} else {
			p++;
		}
	}
#undef append_buf

	return ret;
}

/* versions that use an internal buffer, which is suitable for most
 * scenarios */
static char _atom_buf[BUFSIZ];
char *
atom_to_string(depend_atom *a)
{
	return atom_to_string_r(_atom_buf, sizeof(_atom_buf), a);
}

char *
atom_format(const char *format, const depend_atom *atom)
{
	return atom_format_r(_atom_buf, sizeof(_atom_buf), format, atom);
}

/* qsort compatible callback function */
inline int
atom_compar_cb(const void *l, const void *r)
{
	const depend_atom *al = l;
	const depend_atom *ar = r;

	switch (atom_compare(al, ar)) {
		case EQUAL:  return  0;
		case NEWER:  return -1;
		case OLDER:  return  1;
		default:
		{
			int ret;
			ret = strcmp(al->CATEGORY, ar->CATEGORY);
			if (ret == 0)
				ret = strcasecmp(al->PN, ar->PN);
			return ret;
		}
	}

	/* unreachable */
}
