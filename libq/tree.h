/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _TREE_H
#define _TREE_H 1

#include <dirent.h>
#include <stdbool.h>

#include "atom.h"
#include "set.h"

typedef struct tree_ctx          tree_ctx;
typedef struct tree_cat_ctx      tree_cat_ctx;
typedef struct tree_pkg_ctx      tree_pkg_ctx;
typedef struct tree_pkg_meta     tree_pkg_meta;
typedef struct tree_metadata_xml tree_metadata_xml;

/* VDB context */
struct tree_ctx {
	int portroot_fd;
	int tree_fd;
	DIR *dir;
	struct dirent **cat_de;
	size_t cat_cnt;
	size_t cat_cur;
	void *catsortfunc;
	void *pkgsortfunc;
	bool do_sort:1;
	enum {
		CACHE_UNSET = 0,
		CACHE_METADATA_MD5,
		CACHE_METADATA_PMS,
		CACHE_EBUILD,
		CACHE_VDB,
	} cachetype:3;
	tree_pkg_ctx *ebuilddir_pkg_ctx;
	tree_cat_ctx *ebuilddir_cat_ctx;
	tree_ctx *ebuilddir_ctx;
	char *repo;
};

/* Category context */
struct tree_cat_ctx {
	const char *name;
	int fd;
	DIR *dir;
	tree_ctx *ctx;
	struct dirent **pkg_de;
	size_t pkg_cnt;
	size_t pkg_cur;
};

/* Package context */
struct tree_pkg_ctx {
	const char *name;
	char *slot;
	char *repo;
	size_t slot_len;
	size_t repo_len;
	int fd;
	tree_cat_ctx *cat_ctx;
	depend_atom *atom;
};

/* Ebuild data */
struct tree_pkg_meta {
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
	char *BDEPEND;
	char *_eclasses_;
	char *_md5_;
};

/* Metadata.xml */
struct tree_metadata_xml {
	struct elist {
		char *addr;
		struct elist *next;
	} *email;
};

/* Global helpers */
typedef int (tree_pkg_cb)(tree_pkg_ctx *, void *priv);
typedef int (tree_cat_filter)(tree_cat_ctx *, void *priv);

tree_ctx *tree_open_vdb(const char *sroot, const char *svdb);
tree_ctx *tree_open(const char *sroot, const char *portdir);
void tree_close(tree_ctx *ctx);
int tree_filter_cat(const struct dirent *de);
tree_cat_ctx *tree_open_cat(tree_ctx *ctx, const char *name);
tree_cat_ctx *tree_next_cat(tree_ctx *ctx);
void tree_close_cat(tree_cat_ctx *cat_ctx);
int tree_filter_pkg(const struct dirent *de);
tree_pkg_ctx *tree_open_pkg(tree_cat_ctx *cat_ctx, const char *name);
tree_pkg_ctx *tree_next_pkg(tree_cat_ctx *cat_ctx);
int tree_pkg_vdb_openat(tree_pkg_ctx *pkg_ctx, const char *file, int flags, mode_t mode);
FILE *tree_pkg_vdb_fopenat(tree_pkg_ctx *pkg_ctx, const char *file,
	int flags, mode_t mode, const char *fmode);
#define tree_pkg_vdb_fopenat_ro(pkg_ctx, file) \
	tree_pkg_vdb_fopenat(pkg_ctx, file, O_RDONLY, 0, "r")
#define tree_pkg_vdb_fopenat_rw(pkg_ctx, file) \
	tree_pkg_vdb_fopenat(pkg_ctx, file, O_RDWR | O_CREAT | O_TRUNC, 0644, "w")
bool tree_pkg_vdb_eat(tree_pkg_ctx *pkg_ctx, const char *file, char **bufptr, size_t *buflen);
tree_pkg_meta *tree_pkg_read(tree_pkg_ctx *pkg_ctx);
void tree_close_meta(tree_pkg_meta *cache);
tree_metadata_xml *tree_pkg_metadata(tree_pkg_ctx *pkg_ctx);
void tree_close_metadata(tree_metadata_xml *meta_ctx);
void tree_close_pkg(tree_pkg_ctx *pkg_ctx);
int tree_foreach_pkg(tree_ctx *ctx,
		tree_pkg_cb callback, void *priv, tree_cat_filter filter,
		bool sort, void *catsortfunc, void *pkgsortfunc);
#define tree_foreach_pkg_fast(ctx, cb, priv, filter) \
	tree_foreach_pkg(ctx, cb, priv, filter, false, NULL, NULL);
#define tree_foreach_pkg_sorted(ctx, cb, priv) \
	tree_foreach_pkg(ctx, cb, priv, NULL, true, NULL, NULL);
struct dirent *tree_get_next_dir(DIR *dir);
set *tree_get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv);
depend_atom *tree_get_atom(tree_pkg_ctx *pkg_ctx, bool complete);

#endif
