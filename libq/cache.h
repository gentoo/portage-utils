/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
 */

#ifndef _CACHE_H
#define _CACHE_H 1

#include "atom.h"

typedef struct {
	char *_data;
	char *DEPEND;        /* line 1 */
	char *RDEPEND;
	char *SLOT;
	char *SRC_URI;
	char *RESTRICT;      /* line 5 */
	char *HOMEPAGE;
	char *LICENSE;
	char *DESCRIPTION;
	char *KEYWORDS;
	char *INHERITED;     /* line 10 */
	char *IUSE;
	char *CDEPEND;
	char *PDEPEND;
	char *PROVIDE;       /* line 14 */
	char *EAPI;
	char *PROPERTIES;
	depend_atom *atom;
	/* These are MD5-Cache only */
	char *DEFINED_PHASES;
	char *REQUIRED_USE;
	char *_eclasses_;
	char *_md5_;
} portage_cache;

portage_cache *
cache_read_file(int portcachedir_type, const char *file);
void cache_free(portage_cache *cache);

enum {
	CACHE_EBUILD = 1,
	CACHE_METADATA = 2,
	CACHE_METADATA_PMS = 10,
	CACHE_METADATA_MD5 = 11,
};

#endif
