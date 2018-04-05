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

#ifdef EBUG
static void
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

static depend_atom *
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
	 * separator from PN to PV* */

	ptr = ret->PN;
	while ((ptr = strchr(ptr, '-')) != NULL) {
		ptr++;
		if (*ptr >= '0' && *ptr <= '9')
			break;
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
		--ptr;
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

	/* skip back to the "end" */
	for (ptr = ret->PV; *ptr != '\0' && *ptr != '_'; ptr++)
		;
	ptr--;

	/* allow for 1 optional suffix letter */
	if (*ptr >= 'a' && *ptr <= 'z')
		ret->letter = *ptr--;

	/* eat the trailing version number [.0-9]+ */
	while (ptr > ret->PV) {
		if (*ptr != '.' && !isdigit(*ptr))
			break;
		ptr--;
	}

	if (ptr != ret->PV) {
		/* PV isn't exactly a number */
		ret->PV = ret->PVR = NULL;
	} else {
		ptr = stpcpy(ret->PVR, ret->PV);
		sprintf(ptr, "-r%i", ret->PR_int);
	}

	return ret;
}

static void
atom_implode(depend_atom *atom)
{
	if (!atom)
		errf("Atom is empty !");
	free(atom->suffixes);
#ifndef _USE_CACHE
	free(atom);
#endif
}
