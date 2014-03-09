/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

const char * const booga[] = {"!!!", "!=", "==", ">", "<"};
enum { ERROR=0, NOT_EQUAL, EQUAL, NEWER, OLDER };

static int _atom_compare_match(int ret, atom_operator op)
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
static int atom_compare(const depend_atom *a1, const depend_atom *a2)
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

int atom_compare_str(const char * const s1, const char * const s2);
int atom_compare_str(const char * const s1, const char * const s2)
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
