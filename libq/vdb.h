/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _VDB_H
#define _VDB_H 1

#include <dirent.h>
#include <stdbool.h>

#include "set.h"

typedef struct vdb_ctx vdb_ctx;
typedef struct vdb_cat_ctx vdb_cat_ctx;
typedef struct vdb_pkg_ctx vdb_pkg_ctx;

/* VDB context */
struct vdb_ctx {
	int portroot_fd;
	int vdb_fd;
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
	vdb_pkg_ctx *ebuilddir_pkg_ctx;
	vdb_cat_ctx *ebuilddir_cat_ctx;
	vdb_ctx *ebuilddir_ctx;
	char *repo;
};

/* Category context */
struct vdb_cat_ctx {
	const char *name;
	int fd;
	DIR *dir;
	const vdb_ctx *ctx;
	struct dirent **pkg_de;
	size_t pkg_cnt;
	size_t pkg_cur;
};

/* Package context */
struct vdb_pkg_ctx {
	const char *name;
	char *slot;
	char *repo;
	size_t slot_len;
	size_t repo_len;
	int fd;
	vdb_cat_ctx *cat_ctx;
	depend_atom *atom;
};

/* Global helpers */
typedef int (vdb_pkg_cb)(vdb_pkg_ctx *, void *priv);
typedef int (vdb_cat_filter)(vdb_cat_ctx *, void *priv);

vdb_ctx *vdb_open(const char *sroot, const char *svdb);
vdb_ctx *vdb_open2(const char *sroot, const char *svdb, bool quiet);
void vdb_close(vdb_ctx *ctx);
int vdb_filter_cat(const struct dirent *de);
vdb_cat_ctx *vdb_open_cat(vdb_ctx *ctx, const char *name);
vdb_cat_ctx *vdb_next_cat(vdb_ctx *ctx);
void vdb_close_cat(vdb_cat_ctx *cat_ctx);
int vdb_filter_pkg(const struct dirent *de);
vdb_pkg_ctx *vdb_open_pkg(vdb_cat_ctx *cat_ctx, const char *name);
vdb_pkg_ctx *vdb_next_pkg(vdb_cat_ctx *cat_ctx);
int vdb_pkg_openat(vdb_pkg_ctx *pkg_ctx, const char *file, int flags, mode_t mode);
FILE *vdb_pkg_fopenat(vdb_pkg_ctx *pkg_ctx, const char *file,
	int flags, mode_t mode, const char *fmode);
#define vdb_pkg_fopenat_ro(pkg_ctx, file) \
	vdb_pkg_fopenat(pkg_ctx, file, O_RDONLY, 0, "r")
#define vdb_pkg_fopenat_rw(pkg_ctx, file) \
	vdb_pkg_fopenat(pkg_ctx, file, O_RDWR|O_CREAT|O_TRUNC, 0644, "w")
bool vdb_pkg_eat(vdb_pkg_ctx *pkg_ctx, const char *file, char **bufptr, size_t *buflen);
void vdb_close_pkg(vdb_pkg_ctx *pkg_ctx);
int vdb_foreach_pkg(const char *sroot, const char *svdb,
		vdb_pkg_cb callback, void *priv, vdb_cat_filter filter);
int vdb_foreach_pkg_sorted(const char *sroot, const char *svdb,
		vdb_pkg_cb callback, void *priv);
struct dirent *vdb_get_next_dir(DIR *dir);
set *get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv);
depend_atom *vdb_get_atom(vdb_pkg_ctx *pkg_ctx, bool complete);

#endif
