/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2019-     Fabian Groffen  - <grobian@gentoo.org>
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

vdb_ctx *
vdb_open2(const char *sroot, const char *svdb, bool quiet)
{
	vdb_ctx *ctx = xmalloc(sizeof(*ctx));

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

	ctx->do_sort = false;
	ctx->cat_de = NULL;
	ctx->catsortfunc = alphasort;
	ctx->pkgsortfunc = alphasort;
	ctx->repo = NULL;
	ctx->ebuilddir_ctx = NULL;
	ctx->ebuilddir_pkg_ctx = NULL;
	return ctx;

 cv_error:
	close(ctx->vdb_fd);
 cp_error:
	close(ctx->portroot_fd);
 f_error:
	free(ctx);
	return NULL;
}

vdb_ctx *
vdb_open(const char *sroot, const char *svdb)
{
	return vdb_open2(sroot, svdb, false);
}

void
vdb_close(vdb_ctx *ctx)
{
	closedir(ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->vdb_fd); */
	close(ctx->portroot_fd);
	if (ctx->do_sort)
		scandir_free(ctx->cat_de, ctx->cat_cnt);
	free(ctx);
}

int
vdb_filter_cat(const struct dirent *de)
{
	int i;
	bool founddash;

#ifdef DT_UNKNOWN
	/* cat must be a dir */
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

vdb_cat_ctx *
vdb_open_cat(vdb_ctx *ctx, const char *name)
{
	vdb_cat_ctx *cat_ctx;
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
	cat_ctx->pkg_de = NULL;
	return cat_ctx;
}

vdb_cat_ctx *
vdb_next_cat(vdb_ctx *ctx)
{
	/* search for a category directory */
	vdb_cat_ctx *cat_ctx = NULL;

	if (ctx->do_sort) {
		if (ctx->cat_de == NULL) {
			ctx->cat_cnt = scandirat(ctx->vdb_fd,
					".", &ctx->cat_de, vdb_filter_cat, ctx->catsortfunc);
			ctx->cat_cur = 0;
		}

		while (ctx->cat_cur < ctx->cat_cnt) {
			cat_ctx = vdb_open_cat(ctx, ctx->cat_de[ctx->cat_cur++]->d_name);
			if (!cat_ctx)
				continue;
			break;
		}
	} else {
		/* cheaper "streaming" variant */
		const struct dirent *de;
		do {
			de = readdir(ctx->dir);
			if (!de)
				break;

			if (vdb_filter_cat(de) == 0)
				continue;

			cat_ctx = vdb_open_cat(ctx, de->d_name);
			if (!cat_ctx)
				continue;

			break;
		} while (1);
	}

	return cat_ctx;
}

void
vdb_close_cat(vdb_cat_ctx *cat_ctx)
{
	closedir(cat_ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->fd); */
	if (cat_ctx->ctx->do_sort)
		scandir_free(cat_ctx->pkg_de, cat_ctx->pkg_cnt);
	free(cat_ctx);
}

int
vdb_filter_pkg(const struct dirent *de)
{
	int i;
	bool founddash = false;

	/* PMS 3.1.2 */
	for (i = 0; de->d_name[i] != '\0'; i++) {
		switch (de->d_name[i]) {
			case '_':
				break;
			case '-':
				founddash = true;
				/* fall through */
			case '+':
				if (i)
					break;
				return 0;
			default:
				if ((de->d_name[i] >= 'A' && de->d_name[i] <= 'Z') ||
						(de->d_name[i] >= 'a' && de->d_name[i] <= 'z') ||
						(de->d_name[i] >= '0' && de->d_name[i] <= '9'))
					break;
				if (founddash)
					return 1;
				return 0;
		}
	}

	return i;
}

vdb_pkg_ctx *
vdb_open_pkg(vdb_cat_ctx *cat_ctx, const char *name)
{
	vdb_pkg_ctx *pkg_ctx = xmalloc(sizeof(*pkg_ctx));
	pkg_ctx->name = name;
	pkg_ctx->slot = NULL;
	pkg_ctx->repo = cat_ctx->ctx->repo;
	pkg_ctx->fd = -1;
	pkg_ctx->cat_ctx = cat_ctx;
	pkg_ctx->atom = NULL;
	return pkg_ctx;
}

vdb_pkg_ctx *
vdb_next_pkg(vdb_cat_ctx *cat_ctx)
{
	vdb_pkg_ctx *pkg_ctx = NULL;

	if (cat_ctx->ctx->do_sort) {
		if (cat_ctx->pkg_de == NULL) {
			cat_ctx->pkg_cnt = scandirat(cat_ctx->fd, ".", &cat_ctx->pkg_de,
					vdb_filter_pkg, cat_ctx->ctx->pkgsortfunc);
			cat_ctx->pkg_cur = 0;
		}

		while (cat_ctx->pkg_cur < cat_ctx->pkg_cnt) {
			pkg_ctx =
				vdb_open_pkg(cat_ctx,
						cat_ctx->pkg_de[cat_ctx->pkg_cur++]->d_name);
			if (!pkg_ctx)
				continue;
			break;
		}
	} else {
		const struct dirent *de;
		do {
			de = readdir(cat_ctx->dir);
			if (!de)
				break;

			if (vdb_filter_pkg(de) == 0)
				continue;

			pkg_ctx = vdb_open_pkg(cat_ctx, de->d_name);
			if (!pkg_ctx)
				continue;

			break;
		} while (1);
	}

	return pkg_ctx;
}

int
vdb_pkg_openat(vdb_pkg_ctx *pkg_ctx, const char *file, int flags, mode_t mode)
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
vdb_pkg_fopenat(vdb_pkg_ctx *pkg_ctx, const char *file,
	int flags, mode_t mode, const char *fmode)
{
	FILE *fp;
	int fd;

	fd = vdb_pkg_openat(pkg_ctx, file, flags, mode);
	if (fd == -1)
		return NULL;

	fp = fdopen(fd, fmode);
	if (!fp)
		close(fd);

	return fp;
}

bool
vdb_pkg_eat(vdb_pkg_ctx *pkg_ctx, const char *file, char **bufptr, size_t *buflen)
{
	int fd = vdb_pkg_openat(pkg_ctx, file, O_RDONLY, 0);
	bool ret = eat_file_fd(fd, bufptr, buflen);
	rmspace(*bufptr);
	if (fd != -1)
		close(fd);
	return ret;
}

void
vdb_close_pkg(vdb_pkg_ctx *pkg_ctx)
{
	if (pkg_ctx->fd != -1)
		close(pkg_ctx->fd);
	if (pkg_ctx->atom != NULL)
		atom_implode(pkg_ctx->atom);
	free(pkg_ctx->slot);
	free(pkg_ctx->repo);
	free(pkg_ctx);
}

static int
vdb_foreach_pkg_int(const char *sroot, const char *svdb,
		vdb_pkg_cb callback, void *priv, vdb_cat_filter filter,
		bool sort, void *catsortfunc, void *pkgsortfunc)
{
	vdb_ctx *ctx;
	vdb_cat_ctx *cat_ctx;
	vdb_pkg_ctx *pkg_ctx;
	int ret;

	ctx = vdb_open(sroot, svdb);
	if (!ctx)
		return EXIT_FAILURE;

	ctx->do_sort = sort;
	if (catsortfunc != NULL)
		ctx->catsortfunc = catsortfunc;
	if (pkgsortfunc != NULL)
		ctx->pkgsortfunc = pkgsortfunc;

	ret = 0;
	while ((cat_ctx = vdb_next_cat(ctx))) {
		if (filter && !filter(cat_ctx, priv))
			continue;
		while ((pkg_ctx = vdb_next_pkg(cat_ctx))) {
			ret |= callback(pkg_ctx, priv);
			vdb_close_pkg(pkg_ctx);
		}
		vdb_close_cat(cat_ctx);
	}
	vdb_close(ctx);

	return ret;
}

int
vdb_foreach_pkg(const char *sroot, const char *svdb,
		vdb_pkg_cb callback, void *priv, vdb_cat_filter filter)
{
	return vdb_foreach_pkg_int(sroot, svdb, callback, priv,
			filter, false, NULL, NULL);
}

int
vdb_foreach_pkg_sorted(const char *sroot, const char *svdb,
		vdb_pkg_cb callback, void *priv)
{
	return vdb_foreach_pkg_int(sroot, svdb, callback, priv,
			NULL, true, NULL, NULL);
}

struct dirent *
vdb_get_next_dir(DIR *dir)
{
	/* search for a category directory */
	struct dirent *ret;

next_entry:
	ret = readdir(dir);
	if (ret == NULL) {
		closedir(dir);
		return NULL;
	}

	if (vdb_filter_cat(ret) == 0)
		goto next_entry;

	return ret;
}

depend_atom *
vdb_get_atom(vdb_pkg_ctx *pkg_ctx, bool complete)
{
	if (pkg_ctx->atom == NULL) {
		pkg_ctx->atom = atom_explode(pkg_ctx->name);
		if (pkg_ctx->atom == NULL)
			return NULL;
		pkg_ctx->atom->CATEGORY = (char *)pkg_ctx->cat_ctx->name;
	}

	if (complete) {
		if (pkg_ctx->atom->SLOT == NULL) {
			vdb_pkg_eat(pkg_ctx, "SLOT",
					&pkg_ctx->slot, &pkg_ctx->slot_len);
			pkg_ctx->atom->SLOT = pkg_ctx->slot;
		}
		if (pkg_ctx->atom->REPO == NULL) {
			vdb_pkg_eat(pkg_ctx, "repository",
					&pkg_ctx->repo, &pkg_ctx->repo_len);
			pkg_ctx->atom->REPO = pkg_ctx->repo;
		}
	}

	return pkg_ctx->atom;
}

set *
get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv)
{
	vdb_ctx *ctx;

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

	ctx = vdb_open(sroot, svdb);
	if (!ctx)
		return NULL;

	/* scan the cat first */
	cfd = scandirat(ctx->vdb_fd, ".", &cat, vdb_filter_cat, alphasort);
	if (cfd < 0)
		goto fuckit;

	for (j = 0; j < cfd; j++) {
		dfd = scandirat(ctx->vdb_fd, cat[j]->d_name,
				&pf, vdb_filter_pkg, alphasort);
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
	vdb_close(ctx);
	return cpf;
}
