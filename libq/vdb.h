/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#ifndef _VDB_H
#define _VDB_H 1

#include <dirent.h>

#include "set.h"

/* VDB context */
typedef struct {
	int portroot_fd, vdb_fd;
	DIR *dir;
} q_vdb_ctx;

/* Category context */
typedef struct {
	const char *name;
	int fd;
	DIR *dir;
	const q_vdb_ctx *ctx;
} q_vdb_cat_ctx;

/* Package context */
typedef struct {
	const char *name;
	char *slot, *repo;
	size_t slot_len, repo_len;
	int fd;
	q_vdb_cat_ctx *cat_ctx;
} q_vdb_pkg_ctx;

/* Global helpers */
typedef int (q_vdb_pkg_cb)(q_vdb_pkg_ctx *, void *priv);
typedef int (q_vdb_cat_filter)(q_vdb_cat_ctx *, void *priv);

q_vdb_ctx *q_vdb_open(const char *sroot, const char *svdb);
void q_vdb_close(q_vdb_ctx *ctx);
int q_vdb_filter_cat(const struct dirent *de);
q_vdb_cat_ctx *q_vdb_open_cat(q_vdb_ctx *ctx, const char *name);
q_vdb_cat_ctx *q_vdb_next_cat(q_vdb_ctx *ctx);
void q_vdb_close_cat(q_vdb_cat_ctx *cat_ctx);
int q_vdb_filter_pkg(const struct dirent *de);
q_vdb_pkg_ctx *q_vdb_open_pkg(q_vdb_cat_ctx *cat_ctx, const char *name);
q_vdb_pkg_ctx *q_vdb_next_pkg(q_vdb_cat_ctx *cat_ctx);
int q_vdb_pkg_openat(q_vdb_pkg_ctx *pkg_ctx, const char *file, int flags, mode_t mode);
FILE *q_vdb_pkg_fopenat(q_vdb_pkg_ctx *pkg_ctx, const char *file,
	int flags, mode_t mode, const char *fmode);
#define q_vdb_pkg_fopenat_ro(pkg_ctx, file) \
	q_vdb_pkg_fopenat(pkg_ctx, file, O_RDONLY, 0, "r")
#define q_vdb_pkg_fopenat_rw(pkg_ctx, file) \
	q_vdb_pkg_fopenat(pkg_ctx, file, O_RDWR|O_CREAT|O_TRUNC, 0644, "w")
bool q_vdb_pkg_eat(q_vdb_pkg_ctx *pkg_ctx, const char *file, char **bufptr, size_t *buflen);
void q_vdb_close_pkg(q_vdb_pkg_ctx *pkg_ctx);
int q_vdb_foreach_pkg(const char *sroot, const char *svdb,
		q_vdb_pkg_cb callback, void *priv, q_vdb_cat_filter filter);
int q_vdb_foreach_pkg_sorted(const char *sroot, const char *svdb,
		q_vdb_pkg_cb callback, void *priv);
struct dirent *q_vdb_get_next_dir(DIR *dir);
set *get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv);

#endif
