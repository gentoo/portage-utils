/*
 * Copyright 2005-2020 Gentoo Foundation
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
atom_explode(const char *atom)
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
	len = sizeof(*ret) + (slen * 3);
	ret = xmalloc(len);
	memset(ret, '\0', sizeof(*ret));

	/* assign pointers to the three storage containers */
	ret->CATEGORY = (char *)ret + sizeof(*ret);     /* CAT PF PVR */
	ret->P        = ret->CATEGORY + slen;           /* P   PV     */
	ret->PN       = ret->P + slen;                  /* PN         */

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
	if ((ptr = strstr(ret->CATEGORY, ".ebuild")) != NULL)
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
atom_compare(const depend_atom *data, const depend_atom *query)
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
		/* ^perl -> match anything with a SLOT */
		if (query->SLOT == NULL && data->SLOT == NULL)
			return NOT_EQUAL;
		if (query->SLOT != NULL) {
			if (query->SUBSLOT == NULL) {
				/* ^perl:0 -> match different SLOT */
				if (data->SLOT == NULL ||
						strcmp(query->SLOT, data->SLOT) == 0)
					return NOT_EQUAL;
			} else {
				/* ^perl:0/5.28 -> match SLOT, but different SUBSLOT */
				if (data->SLOT == NULL ||
						strcmp(query->SLOT, data->SLOT) != 0)
					return NOT_EQUAL;
				if (data->SUBSLOT == NULL ||
						strcmp(query->SUBSLOT, data->SUBSLOT) == 0)
					return NOT_EQUAL;
			}
		}
		bl_op = ATOM_BL_NONE;  /* ease work below */
	} else if (query->SLOT != NULL) {
		/* check SLOT only when query side has it */
		if (data->SLOT == NULL) {
			if (bl_op == ATOM_BL_NONE)
				return NOT_EQUAL;
		} else {
			if (strcmp(query->SLOT, data->SLOT) != 0) {
				/* slot has differs */
				if (bl_op == ATOM_BL_NONE)
					return NOT_EQUAL;
			} else {
				if (query->SUBSLOT != NULL) {
					if (data->SUBSLOT == NULL) {
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
	if (query->REPO != NULL) {
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
		char *s1, *s2;
		uint64_t n1, n2;
		/* first we compare the version [1.0]z_alpha1 */
		s1 = data->PV;
		s2 = query->PV;
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
			if (!query->letter && !ver_bits)
				return _atom_compare_match(EQUAL, pfx_op);
		}
		if (data->letter < query->letter)
			return _atom_compare_match(OLDER, pfx_op);
		if (data->letter > query->letter)
			return _atom_compare_match(NEWER, pfx_op);
		/* find differing suffixes 1.0z[_alpha1] */
		const atom_suffix *as1 = &data->suffixes[0];
		const atom_suffix *as2 = &query->suffixes[0];
		while (as1->suffix == as2->suffix) {
			if (as1->suffix == VER_NORM ||
			    as2->suffix == VER_NORM)
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
	} else if (data->PV || query->PV)
		return EQUAL;

	/* Make sure the -r# is the same. */
	if ((sfx_op == ATOM_OP_STAR && !query->PR_int) ||
	    pfx_op == ATOM_OP_PV_EQUAL ||
	    data->PR_int == query->PR_int)
		return _atom_compare_match(EQUAL, pfx_op);
	else if (data->PR_int < query->PR_int)
		return _atom_compare_match(OLDER, pfx_op);
	else
		return _atom_compare_match(NEWER, pfx_op);
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
				a->SUBSLOT ? "/" : "", a->SUBSLOT ? a->SUBSLOT : "",
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
								CYAN, atom->PV, NORM);
					if (atom->PR_int)
						append_buf(buf, buflen,"%s-r%d%s",
								CYAN, atom->PR_int, NORM);
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
					if (showit || atom->SUBSLOT)
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
						append_buf(buf, buflen, "%s", "[");
						for (ud = atom->usedeps; ud != NULL; ud = ud->next)
							append_buf(buf, buflen, "%s%s%s%s%s%s",
									MAGENTA, atom_usecond_str[ud->pfx_cond],
									ud->use, atom_usecond_str[ud->sfx_cond],
									NORM, ud->next == NULL ? "]" : ",");
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
