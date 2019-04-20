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
#include "vdb.h"

typedef struct cache_ctx {
	q_vdb_ctx *dir_ctx;
	enum { CACHE_UNSET = 0, CACHE_METADATA_MD5, CACHE_METADATA_PMS } cachetype;
} cache_ctx;
#define cache_cat_ctx q_vdb_cat_ctx
#define cache_pkg_ctx q_vdb_pkg_ctx

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
	/* These are MD5-Cache only */
	char *DEFINED_PHASES;
	char *REQUIRED_USE;
	char *_eclasses_;
	char *_md5_;
} cache_pkg_meta;

typedef int (cache_pkg_cb)(cache_pkg_ctx *, void *priv);
typedef int (cache_cat_filter)(cache_cat_ctx *, void *priv);

cache_ctx *cache_open(const char *sroot, const char *portdir);
void cache_close(cache_ctx *ctx);
cache_cat_ctx *cache_open_cat(cache_ctx *ctx, const char *name);
cache_cat_ctx *cache_next_cat(cache_ctx *ctx);
void cache_close_cat(cache_cat_ctx *cat_ctx);
cache_pkg_ctx *cache_open_pkg(cache_cat_ctx *cat_ctx, const char *name);
cache_pkg_ctx *cache_next_pkg(cache_cat_ctx *cat_ctx);
cache_pkg_meta *cache_pkg_read(cache_pkg_ctx *pkg_ctx);
void cache_close_meta(cache_pkg_meta *cache);
void cache_close_pkg(cache_pkg_ctx *pkg_ctx);
int cache_foreach_pkg(const char *sroot, const char *portdir,
		cache_pkg_cb callback, void *priv, cache_cat_filter filter);
int cache_foreach_pkg_sorted(const char *sroot, const char *portdir,
		cache_pkg_cb callback, void *priv,
		void *catsortfunc, void *pkgsortfunc);

#endif
