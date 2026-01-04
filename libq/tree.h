/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _TREE_H
#define _TREE_H 1

#include <dirent.h>
#include <stdbool.h>
#include <stddef.h>

#include "atom.h"
#include "set.h"

typedef struct tree_ctx          tree_ctx;
typedef struct tree_cat_ctx      tree_cat_ctx;
typedef struct tree_pkg_ctx      tree_pkg_ctx;
typedef struct tree_pkg_meta     tree_pkg_meta;
typedef struct tree_metadata_xml tree_metadata_xml;
typedef struct tree_match_ctx    tree_match_ctx;

/* tree context */
struct tree_ctx {
	int portroot_fd;
	int tree_fd;
	char path[_Q_PATH_MAX];
	DIR *dir;
	struct dirent **cat_de;
	size_t cat_cnt;
	size_t cat_cur;
	bool do_sort:1;
	enum {
		TREE_UNSET = 0,
		TREE_METADATA_GTREE,
		TREE_METADATA_MD5,
		TREE_METADATA_PMS,
		TREE_EBUILD,
		TREE_VDB,
		TREE_PACKAGES,
		TREE_BINPKGS,
	} treetype:3;
	tree_ctx *subtree;
	tree_pkg_ctx *ebuilddir_pkg_ctx;
	tree_cat_ctx *ebuilddir_cat_ctx;
	tree_ctx *ebuilddir_ctx;
	char *repo;
	char *pkgs;
	size_t pkgslen;
	const depend_atom *query_atom;
	struct tree_cache {
		char *store;
		size_t storesize;
		set *categories;
		bool all_categories:1;
	} cache;
};

/* Category context */
struct tree_cat_ctx {
	const char *name;
	int fd;
	DIR *dir;
	tree_ctx *ctx;
	tree_pkg_ctx **pkg_ctxs;
	size_t pkg_cnt;
	size_t pkg_cur;
	size_t pkg_siz;
};

/* Package context */
struct tree_pkg_ctx {
	const char *name;
	char *slot;
	char *repo;
	size_t slot_len;
	size_t repo_len;
	int fd;
	bool cached:1;
	bool binpkg_isgpkg:1;
	bool binpkg_ismulti:1;  /* for path reconstruction */
	tree_cat_ctx *cat_ctx;
	depend_atom *atom;
	tree_pkg_meta *meta;
};

/* Ebuild data */
struct tree_pkg_meta {
#define Q__data Q_DEPEND  /* ptr to first member of the struct */
	char *Q_DEPEND;        /* line 1 */
	char *Q_RDEPEND;
	char *Q_SLOT;
	char *Q_SRC_URI;
	char *Q_RESTRICT;      /* line 5 */
	char *Q_HOMEPAGE;
	char *Q_LICENSE;
	char *Q_DESCRIPTION;
	char *Q_KEYWORDS;
	char *Q_INHERITED;     /* line 10 */
	char *Q_IUSE;
	char *Q_CDEPEND;
	char *Q_PDEPEND;
	char *Q_PROVIDE;       /* line 14 */
	char *Q_EAPI;
	char *Q_PROPERTIES;    /* last line from metadata */
	char *Q_BDEPEND;
	char *Q_IDEPEND;
	/* binpkgs/vdb */
	char *Q_DEFINED_PHASES;
	char *Q_REQUIRED_USE;
	char *Q_CONTENTS;
	char *Q_USE;
	char *Q_EPREFIX;
	char *Q_PATH;     /* binpkg-multi-instance */
	char *Q_BUILD_ID; /* binpkg-multi-instance */
	char *Q_repository;
	char *Q_MD5;
	char *Q_SHA1;
#define Q_SIZE Q_SRC_URI
	/* These are MD5-Cache only */
	char *Q__eclasses_;
	char *Q__md5_;
#define Q__last Q__md5_  /* ptr to last data member in struct */

	/* for memory allocations backing the pointers above */
	struct tree_pkg_meta_ll {
		char                    *ptr;
		size_t                   len;
		size_t                   pos;
		struct tree_pkg_meta_ll *next;
	}    *storage;
};

/* Metadata.xml */
struct tree_metadata_xml {
	struct elist {
		char *addr;
		struct elist *next;
	} *email;
};

/* used with tree_match_atom, both atom and meta are fully materialised
 * (populated and deep copied) when set */
struct tree_match_ctx {
	depend_atom *atom;
	tree_pkg_ctx *pkg;
	tree_pkg_meta *meta;
	char path[_Q_PATH_MAX + 48];
	tree_match_ctx *next;
	char free_atom:1;
	char free_meta:1;
};

/* foreach pkg callback function signature */
typedef int (tree_pkg_cb)(tree_pkg_ctx *, void *priv);

tree_ctx *tree_open(const char *sroot, const char *portdir);
tree_ctx *tree_open_vdb(const char *sroot, const char *svdb);
tree_ctx *tree_open_ebuild(const char *sroot, const char *portdir);
tree_ctx *tree_open_binpkg(const char *sroot, const char *spkg);
#define tree_open_gtree(R,T) tree_open_gtree_int(R,T,false)
tree_ctx *tree_open_gtree_int(const char *sroot, const char *tar, bool quiet);
void tree_close(tree_ctx *ctx);
tree_cat_ctx *tree_open_cat(tree_ctx *ctx, const char *name);
void tree_close_cat(tree_cat_ctx *cat_ctx);
tree_pkg_ctx *tree_open_pkg(tree_cat_ctx *cat_ctx, const char *name);
tree_pkg_ctx *tree_next_pkg(tree_cat_ctx *cat_ctx);
char *tree_pkg_meta_get_int(tree_pkg_ctx *pkg_ctx, size_t offset, const char *key);
#define tree_pkg_meta_get(P,X) \
	tree_pkg_meta_get_int(P, offsetof(tree_pkg_meta, Q_##X), #X)
tree_metadata_xml *tree_pkg_metadata(tree_pkg_ctx *pkg_ctx);
void tree_close_metadata(tree_metadata_xml *meta_ctx);
void tree_close_pkg(tree_pkg_ctx *pkg_ctx);
int tree_foreach_pkg(tree_ctx *ctx, tree_pkg_cb callback, void *priv,
		bool sort, const depend_atom *query);
#define tree_foreach_pkg_fast(ctx, cb, priv, query) \
	tree_foreach_pkg(ctx, cb, priv, false, query)
#define tree_foreach_pkg_sorted(ctx, cb, priv, query) \
	tree_foreach_pkg(ctx, cb, priv, true, query)
set *tree_get_atoms(tree_ctx *ctx, bool fullcpv, set *satoms);
depend_atom *tree_get_atom(tree_pkg_ctx *pkg_ctx, bool complete);
tree_match_ctx *tree_match_atom(tree_ctx *t, const depend_atom *q, int flags);
#define TREE_MATCH_FULL_ATOM  (1<<1)
#define TREE_MATCH_METADATA   (1<<2)
#define TREE_MATCH_LATEST     (1<<3)
#define TREE_MATCH_FIRST      (1<<4)
#define TREE_MATCH_VIRTUAL    (1<<5)
#define TREE_MATCH_ACCT       (1<<6)
#define TREE_MATCH_DEFAULT    (TREE_MATCH_VIRTUAL | TREE_MATCH_ACCT)
void tree_match_close(tree_match_ctx *t);

#endif
