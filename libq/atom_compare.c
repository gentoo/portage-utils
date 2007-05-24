/*
 * Copyright 2005-006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/atom_compare.c,v 1.3 2007/05/24 14:47:19 solar Exp $
 *
 * Copyright 2005-2007 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2007 Mike Frysinger  - <vapier@gentoo.org>
 */

/*
typedef struct {
	char *CATEGORY;
	char *PN;
	int PR_int;
	char *PV, *PVR;
	char *P;
} depend_atom;
*/

const char *booga[] = {"!!!", "!=", "==", ">", "<"};
enum { ERROR=0, NOT_EQUAL, EQUAL, NEWER, OLDER };
/* a1 <return value> a2
 * foo-1 <EQUAL> foo-1
 * foo-1 <OLDER> foo-2
 * foo-1 <NOT_EQUAL> bar-1
 */
int atom_compare(const depend_atom * const a1, const depend_atom * const a2);
int atom_compare(const depend_atom * const a1, const depend_atom * const a2)
{
	/* check category */
	if (a1->CATEGORY && a2->CATEGORY) {
		if (strcmp(a1->CATEGORY, a2->CATEGORY))
			return NOT_EQUAL;
	} else if (a1->CATEGORY || a2->CATEGORY)
		return NOT_EQUAL;

	/* check name */
	if (a1->PN && a2->PN) {
		if (strcmp(a1->PN, a2->PN))
			return NOT_EQUAL;
	} else if (a1->PN || a2->PN)
		return NOT_EQUAL;

	/* check version */
	if (a1->PV && a2->PV) {
		char *s1, *s2;
		unsigned int n1, n2;
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
					return OLDER;
				else if (*s2 == '0' && isdigit(*s1))
					return NEWER;
			}
			n1 = (s1 ? atol(s1) : 0);
			n2 = (s2 ? atol(s2) : 0);
			if (n1 < n2)
				return OLDER;
			else if (n1 > n2)
				return NEWER;
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
		if (a1->letter < a2->letter)
			return OLDER;
		else if (a1->letter > a2->letter)
			return NEWER;
		/* compare suffixes 1.0z[_alpha]1 */
		n1 = a1->suffix;
		n2 = a2->suffix;
		if (n1 < n2)
			return OLDER;
		else if (n1 > n2)
			return NEWER;
		/* compare suffix number (assume only integers) 1.0z_alpha[1] */
		if (n1 != VER_NORM) {
			s1 = strchr(a1->PV, '_') + strlen(atom_suffixes_str[n1]);
			s2 = strchr(a2->PV, '_') + strlen(atom_suffixes_str[n2]);
			n1 = (*s1 ? atol(s1) : 0);
			n2 = (*s2 ? atol(s2) : 0);
			if (n1 < n2)
				return OLDER;
			else if (n1 > n2)
				return NEWER;
		}
		/* fall through to -r# check below */
	} else if (a1->PV || a2->PV)
		return NOT_EQUAL;

	if (a1->PR_int == a2->PR_int)
		return EQUAL;
	else if (a1->PR_int < a2->PR_int)
		return OLDER;
	else
		return NEWER;
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
