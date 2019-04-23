/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#include "main.h"
#include "rmspace.h"
#include "scandirat.h"
#include "eat_file.h"
#include "set.h"
#include "atom.h"
#include "vdb.h"

#include <ctype.h>
#include <xalloc.h>

q_vdb_ctx *
q_vdb_open2(const char *sroot, const char *svdb, bool quiet)
{
	q_vdb_ctx *ctx = xmalloc(sizeof(*ctx));

	ctx->portroot_fd = open(sroot, O_RDONLY|O_CLOEXEC|O_PATH);
	if (ctx->portroot_fd == -1) {
		if (!quiet)
			warnp("could not open root: %s", sroot);
		goto f_error;
	}

	/* Skip the leading slash */
	svdb++;
	if (*svdb == '\0')
		svdb = ".";
	/* Cannot use O_PATH as we want to use fdopendir() */
	ctx->vdb_fd = openat(ctx->portroot_fd, svdb, O_RDONLY|O_CLOEXEC);
	if (ctx->vdb_fd == -1) {
		if (!quiet)
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

q_vdb_ctx *
q_vdb_open(const char *sroot, const char *svdb)
{
	return q_vdb_open2(sroot, svdb, false);
}

void
q_vdb_close(q_vdb_ctx *ctx)
{
	closedir(ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->vdb_fd); */
	close(ctx->portroot_fd);
	free(ctx);
}

int
q_vdb_filter_cat(const struct dirent *de)
{
	int i;
	bool founddash;

#ifdef DT_UNKNOWN
	if (de->d_type != DT_UNKNOWN &&
	    de->d_type != DT_DIR &&
	    de->d_type != DT_LNK)
		return 0;
#endif

	/* PMS 3.1.1 */
	founddash = false;
	for (i = 0; de->d_name[i] != '\0'; i++) {
		switch (de->d_name[i]) {
			case '_':
				break;
			case '-':
				founddash = true;
				/* fall through */
			case '+':
			case '.':
				if (i)
					break;
				return 0;
			default:
				if ((de->d_name[i] >= 'A' && de->d_name[i] <= 'Z') ||
						(de->d_name[i] >= 'a' && de->d_name[i] <= 'z') ||
						(de->d_name[i] >= '0' && de->d_name[i] <= '9'))
					break;
				return 0;
		}
	}
	if (!founddash && strcmp(de->d_name, "virtual") != 0)
		return 0;

	return i;
}

q_vdb_cat_ctx *
q_vdb_open_cat(q_vdb_ctx *ctx, const char *name)
{
	q_vdb_cat_ctx *cat_ctx;
	int fd;
	DIR *dir;

	/* Cannot use O_PATH as we want to use fdopendir() */
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

q_vdb_cat_ctx *
q_vdb_next_cat(q_vdb_ctx *ctx)
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

void
q_vdb_close_cat(q_vdb_cat_ctx *cat_ctx)
{
	closedir(cat_ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->fd); */
	free(cat_ctx);
}

int
q_vdb_filter_pkg(const struct dirent *de)
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

q_vdb_pkg_ctx *
q_vdb_open_pkg(q_vdb_cat_ctx *cat_ctx, const char *name)
{
	q_vdb_pkg_ctx *pkg_ctx = xmalloc(sizeof(*pkg_ctx));
	pkg_ctx->name = name;
	pkg_ctx->slot = NULL;
	pkg_ctx->repo = NULL;
	pkg_ctx->fd = -1;
	pkg_ctx->cat_ctx = cat_ctx;
	return pkg_ctx;
}

q_vdb_pkg_ctx *
q_vdb_next_pkg(q_vdb_cat_ctx *cat_ctx)
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

int
q_vdb_pkg_openat(q_vdb_pkg_ctx *pkg_ctx, const char *file, int flags, mode_t mode)
{
	if (pkg_ctx->fd == -1) {
		pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
				O_RDONLY|O_CLOEXEC|O_PATH);
		if (pkg_ctx->fd == -1)
			return -1;
	}

	return openat(pkg_ctx->fd, file, flags|O_CLOEXEC, mode);
}

FILE *
q_vdb_pkg_fopenat(q_vdb_pkg_ctx *pkg_ctx, const char *file,
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

bool
q_vdb_pkg_eat(q_vdb_pkg_ctx *pkg_ctx, const char *file, char **bufptr, size_t *buflen)
{
	int fd = q_vdb_pkg_openat(pkg_ctx, file, O_RDONLY, 0);
	bool ret = eat_file_fd(fd, bufptr, buflen);
	rmspace(*bufptr);
	if (fd != -1)
		close(fd);
	return ret;
}

void
q_vdb_close_pkg(q_vdb_pkg_ctx *pkg_ctx)
{
	if (pkg_ctx->fd != -1)
		close(pkg_ctx->fd);
	free(pkg_ctx->slot);
	free(pkg_ctx->repo);
	free(pkg_ctx);
}

int
q_vdb_foreach_pkg(const char *sroot, const char *svdb,
		q_vdb_pkg_cb callback, void *priv, q_vdb_cat_filter filter)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	int ret;

	ctx = q_vdb_open(sroot, svdb);
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

int
q_vdb_foreach_pkg_sorted(const char *sroot, const char *svdb,
		q_vdb_pkg_cb callback, void *priv)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	int ret = 0;
	int c, p, cat_cnt, pkg_cnt;
	struct dirent **cat_de, **pkg_de;

	ctx = q_vdb_open(sroot, svdb);
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

struct dirent *
q_vdb_get_next_dir(DIR *dir)
{
	/* search for a category directory */
	struct dirent *ret;

next_entry:
	ret = readdir(dir);
	if (ret == NULL) {
		closedir(dir);
		return NULL;
	}

	if (q_vdb_filter_cat(ret) == 0)
		goto next_entry;

	return ret;
}

set *
get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv)
{
	q_vdb_ctx *ctx;

	int cfd, j;
	int dfd, i;

	char buf[_Q_PATH_MAX];
	char slot[_Q_PATH_MAX];
	char *slotp = slot;
	size_t slot_len;

	struct dirent **cat;
	struct dirent **pf;

	depend_atom *atom = NULL;
	set *cpf = NULL;

	ctx = q_vdb_open(sroot, svdb);
	if (!ctx)
		return NULL;

	/* scan the cat first */
	cfd = scandirat(ctx->vdb_fd, ".", &cat, q_vdb_filter_cat, alphasort);
	if (cfd < 0)
		goto fuckit;

	for (j = 0; j < cfd; j++) {
		dfd = scandirat(ctx->vdb_fd, cat[j]->d_name,
				&pf, q_vdb_filter_pkg, alphasort);
		if (dfd < 0)
			continue;
		for (i = 0; i < dfd; i++) {
			int blen = snprintf(buf, sizeof(buf), "%s/%s/SLOT",
					cat[j]->d_name, pf[i]->d_name);
			if (blen < 0 || (size_t)blen >= sizeof(buf)) {
				warnf("unable to parse long package: %s/%s",
						cat[j]->d_name, pf[i]->d_name);
				continue;
			}

			/* Chop the SLOT for the atom parsing. */
			buf[blen - 5] = '\0';
			if ((atom = atom_explode(buf)) == NULL)
				continue;
			/* Restore the SLOT. */
			buf[blen - 5] = '/';

			slot_len = sizeof(slot);
			eat_file_at(ctx->vdb_fd, buf, &slotp, &slot_len);
			rmspace(slot);

			if (fullcpv) {
				if (atom->PR_int)
					snprintf(buf, sizeof(buf), "%s/%s-%s-r%i",
							atom->CATEGORY, atom->PN, atom->PV, atom->PR_int);
				else
					snprintf(buf, sizeof(buf), "%s/%s-%s",
							atom->CATEGORY, atom->PN, atom->PV);
			} else {
				snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			}
			atom_implode(atom);
			cpf = add_set(buf, cpf);
		}
		scandir_free(pf, dfd);
	}
	scandir_free(cat, cfd);

 fuckit:
	q_vdb_close(ctx);
	return cpf;
}
