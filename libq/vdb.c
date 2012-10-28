/*
 * Copyright 2005-2011 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/vdb.c,v 1.5 2012/10/28 06:27:59 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2011 Mike Frysinger  - <vapier@gentoo.org>
 */

/*
 * VDB context
 */

typedef struct {
	int portroot_fd, vdb_fd;
	DIR *dir;
} q_vdb_ctx;

_q_static q_vdb_ctx *q_vdb_open(/*const char *sroot, const char *svdb*/void)
{
	q_vdb_ctx *ctx = xmalloc(sizeof(*ctx));
	const char *sroot = NULL;
	const char *svdb = NULL;

	if (!sroot)
		sroot = portroot;
	ctx->portroot_fd = open(sroot, O_RDONLY|O_CLOEXEC);
	if (ctx->portroot_fd == -1) {
		warnp("could not open root: %s", sroot);
		goto f_error;
	}

	if (!svdb)
		svdb = portvdb;
	/* Skip the leading slash */
	svdb++;
	if (*svdb == '\0')
		svdb = ".";
	ctx->vdb_fd = openat(ctx->portroot_fd, svdb, O_RDONLY|O_CLOEXEC);
	if (ctx->vdb_fd == -1) {
		warnp("could not open vdb: %s (in root %s)", svdb, sroot);
		goto cp_error;
	}

	ctx->dir = fdopendir(ctx->vdb_fd);
	if (ctx->dir == NULL)
		goto cv_error;

	return ctx;

 cv_error:
	close(ctx->vdb_fd);
 cp_error:
	close(ctx->portroot_fd);
 f_error:
	free(ctx);
	return NULL;
}

_q_static void q_vdb_close(q_vdb_ctx *ctx)
{
	closedir(ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->vdb_fd); */
	close(ctx->portroot_fd);
	free(ctx);
}

/*
 * Category context
 */

typedef struct {
	const char *name;
	int fd;
	DIR *dir;
	const q_vdb_ctx *ctx;
} q_vdb_cat_ctx;

_q_static int q_vdb_filter_cat(const struct dirent *de)
{
	int i;

#ifdef DT_UNKNOWN
	if (de->d_type != DT_UNKNOWN &&
	    de->d_type != DT_DIR &&
	    de->d_type != DT_LNK)
		return 0;
#endif

	if (de->d_name[0] == '.' || de->d_name[0] == '-')
		return 0;

	for (i = 0; de->d_name[i]; ++i) {
		if (!isalnum(de->d_name[i])) { /* [A-Za-z0-9+_.-] */
			switch (de->d_name[i]) {
				case '+':
				case '_':
				case '.':
				case '-':
					if (i)
						break;
				default:
					return 0;
			}
		}
	}
	return i;
}

_q_static q_vdb_cat_ctx *q_vdb_open_cat(q_vdb_ctx *ctx, const char *name)
{
	q_vdb_cat_ctx *cat_ctx;
	int fd;
	DIR *dir;

	fd = openat(ctx->vdb_fd, name, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return NULL;

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return NULL;
	}

	cat_ctx = xmalloc(sizeof(*cat_ctx));
	cat_ctx->name = name;
	cat_ctx->fd = fd;
	cat_ctx->dir = dir;
	cat_ctx->ctx = ctx;
	return cat_ctx;
}

_q_static q_vdb_cat_ctx *q_vdb_next_cat(q_vdb_ctx *ctx)
{
	/* search for a category directory */
	q_vdb_cat_ctx *cat_ctx;
	const struct dirent *de;

 next_cat:
	de = readdir(ctx->dir);
	if (!de) {
		q_vdb_close(ctx);
		return NULL;
	}

	if (q_vdb_filter_cat(de) == 0)
		goto next_cat;

	cat_ctx = q_vdb_open_cat(ctx, de->d_name);
	if (!cat_ctx)
		goto next_cat;

	return cat_ctx;
}

_q_static void q_vdb_close_cat(q_vdb_cat_ctx *cat_ctx)
{
	closedir(cat_ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->fd); */
	free(cat_ctx);
}

/*
 * Package context
 */

typedef struct {
	const char *name;
	const char *slot;
	int fd;
	q_vdb_cat_ctx *cat_ctx;
} q_vdb_pkg_ctx;

_q_static int q_vdb_filter_pkg(const struct dirent *de)
{
#ifdef DT_UNKNOWN
	if (de->d_type != DT_UNKNOWN &&
	    de->d_type != DT_DIR &&
	    de->d_type != DT_LNK)
		return 0;
#endif

	if (de->d_name[0] == '.' || de->d_name[0] == '-')
		return 0;

	return 1;
}

_q_static q_vdb_pkg_ctx *q_vdb_open_pkg(q_vdb_cat_ctx *cat_ctx, const char *name)
{
	q_vdb_pkg_ctx *pkg_ctx;
	int fd;

	fd = openat(cat_ctx->fd, name, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return NULL;

	pkg_ctx = xmalloc(sizeof(*pkg_ctx));
	pkg_ctx->name = name;
	pkg_ctx->slot = NULL;
	pkg_ctx->fd = fd;
	pkg_ctx->cat_ctx = cat_ctx;
	return pkg_ctx;
}

_q_static q_vdb_pkg_ctx *q_vdb_next_pkg(q_vdb_cat_ctx *cat_ctx)
{
	q_vdb_pkg_ctx *pkg_ctx;
	const struct dirent *de;

 next_pkg:
	de = readdir(cat_ctx->dir);
	if (!de) {
		q_vdb_close_cat(cat_ctx);
		return NULL;
	}

	if (q_vdb_filter_pkg(de) == 0)
		goto next_pkg;

	pkg_ctx = q_vdb_open_pkg(cat_ctx, de->d_name);
	if (!pkg_ctx)
		goto next_pkg;

	return pkg_ctx;
}

#define q_vdb_pkg_openat(pkg_ctx, file, flags, mode...) \
	openat((pkg_ctx)->fd, file, (flags)|O_CLOEXEC, ## mode)
_q_static FILE *q_vdb_pkg_fopenat(q_vdb_pkg_ctx *pkg_ctx, const char *file,
	int flags, mode_t mode, const char *fmode)
{
	FILE *fp;
	int fd;

	fd = q_vdb_pkg_openat(pkg_ctx, file, flags, mode);
	if (fd == -1)
		return NULL;

	fp = fdopen(fd, fmode);
	if (!fp)
		close(fd);

	return fp;
}
#define q_vdb_pkg_fopenat_ro(pkg_ctx, file) q_vdb_pkg_fopenat(pkg_ctx, file, O_RDONLY, 0, "r")
#define q_vdb_pkg_fopenat_rw(pkg_ctx, file) q_vdb_pkg_fopenat(pkg_ctx, file, O_RDWR|O_CREAT|O_TRUNC, 0644, "w")

_q_static void q_vdb_close_pkg(q_vdb_pkg_ctx *pkg_ctx)
{
	close(pkg_ctx->fd);
	free(pkg_ctx);
}

/*
 * Global helpers
 */

typedef int (q_vdb_pkg_cb)(q_vdb_pkg_ctx *, void *priv);
typedef int (q_vdb_cat_filter)(q_vdb_cat_ctx *, void *priv);

_q_static int q_vdb_foreach_pkg(q_vdb_pkg_cb callback, void *priv, q_vdb_cat_filter filter)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	int ret;

	ctx = q_vdb_open();
	if (!ctx)
		return EXIT_FAILURE;

	ret = 0;
	while ((cat_ctx = q_vdb_next_cat(ctx))) {
		if (filter && !filter(cat_ctx, priv))
			continue;
		while ((pkg_ctx = q_vdb_next_pkg(cat_ctx))) {
			ret |= callback(pkg_ctx, priv);
			q_vdb_close_pkg(pkg_ctx);
		}
	}

	return ret;
}

_q_static int q_vdb_foreach_pkg_sorted(q_vdb_pkg_cb callback, void *priv)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	int ret = 0;
	int c, p, cat_cnt, pkg_cnt;
	struct dirent **cat_de, **pkg_de;

	ctx = q_vdb_open();
	if (!ctx)
		return EXIT_FAILURE;

	cat_cnt = scandirat(ctx->vdb_fd, ".", &cat_de, q_vdb_filter_cat, alphasort);
	for (c = 0; c < cat_cnt; ++c) {
		cat_ctx = q_vdb_open_cat(ctx, cat_de[c]->d_name);
		if (!cat_ctx)
			continue;

		pkg_cnt = scandirat(ctx->vdb_fd, cat_de[c]->d_name, &pkg_de, q_vdb_filter_pkg, alphasort);
		for (p = 0; p < pkg_cnt; ++p) {
			if (pkg_de[p]->d_name[0] == '-')
				continue;

			pkg_ctx = q_vdb_open_pkg(cat_ctx, pkg_de[p]->d_name);
			if (!pkg_ctx)
				continue;

			ret |= callback(pkg_ctx, priv);

			q_vdb_close_pkg(pkg_ctx);
		}
		scandir_free(pkg_de, pkg_cnt);

		q_vdb_close_cat(cat_ctx);
	}
	scandir_free(cat_de, cat_cnt);

	q_vdb_close(ctx);
	return ret;
}
