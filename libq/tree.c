/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <xalloc.h>

#if defined(ENABLE_GPKG) || defined(ENABLE_GTREE)
# include <archive.h>
# include <archive_entry.h>
#endif

#include "atom.h"
#include "eat_file.h"
#include "hash.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xpak.h"


static int tree_pkg_compar(const void *l, const void *r);
static void tree_close_meta(tree_pkg_meta *cache);

static tree_ctx *
tree_open_int(const char *sroot, const char *tdir, bool quiet)
{
	tree_ctx *ctx = xzalloc(sizeof(*ctx));

	ctx->portroot_fd = open(sroot, O_RDONLY | O_CLOEXEC | O_PATH);
	if (ctx->portroot_fd == -1) {
		if (!quiet)
			warnp("could not open root: %s", sroot);
		goto f_error;
	}

	/* Skip the leading slash */
	tdir++;
	if (*tdir == '\0')
		tdir = ".";
	/* Cannot use O_PATH as we want to use fdopendir() */
	ctx->tree_fd = openat(ctx->portroot_fd, tdir, O_RDONLY | O_CLOEXEC);
	if (ctx->tree_fd == -1) {
		if (!quiet)
			warnp("could not open tree: %s (in root %s)", tdir, sroot);
		goto cp_error;
	}

	if (sroot[0] == '/' && sroot[1] == '\0')
		sroot = "";
	snprintf(ctx->path, sizeof(ctx->path), "%s/%s", sroot, tdir);

	ctx->dir = fdopendir(ctx->tree_fd);
	if (ctx->dir == NULL)
		goto cv_error;

	ctx->do_sort = false;
	return ctx;

 cv_error:
	close(ctx->tree_fd);
 cp_error:
	close(ctx->portroot_fd);
 f_error:
	free(ctx);
	return NULL;
}

static const char portcachedir_pms[] = "metadata/cache";
static const char portcachedir_md5[] = "metadata/md5-cache";
#ifdef ENABLE_GTREE
static const char portcachedir_gt1[] = "metadata/repo.gtree.tar";
#endif
static const char portrepo_name[]    = "profiles/repo_name";
tree_ctx *
tree_open(const char *sroot, const char *portdir)
{
	tree_ctx *ret;
	char buf[_Q_PATH_MAX];

	ret = tree_open_ebuild(sroot, portdir);
	if (ret == NULL)
	{
		warnf("could not open repository at %s (under root %s)",
			  portdir, sroot);

		return NULL;
	}

	/* look for cache trees to speed things up */
#ifdef ENABLE_GTREE
	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_gt1);
	ret->subtree = tree_open_gtree_int(sroot, buf, true);
	if (ret->subtree != NULL) {
		ret->subtree->treetype = TREE_METADATA_GTREE;
		return ret;
	}
#endif

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_md5);
	ret->subtree = tree_open_int(sroot, buf, true);
	if (ret->subtree != NULL) {
		ret->subtree->treetype = TREE_METADATA_MD5;
		ret->subtree->cache.categories = create_set();
		return ret;
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_pms);
	ret->subtree = tree_open_int(sroot, buf, true);
	if (ret->subtree != NULL) {
		ret->subtree->treetype = TREE_METADATA_PMS;
		ret->subtree->cache.categories = create_set();
		return ret;
	}

	return ret;
}

tree_ctx *
tree_open_vdb(const char *sroot, const char *svdb)
{
	tree_ctx *ret = tree_open_int(sroot, svdb, false);
	if (ret != NULL)
		ret->treetype = TREE_VDB;
	return ret;
}

tree_ctx *
tree_open_ebuild(const char *sroot, const char *portdir)
{
	tree_ctx *ret = tree_open_int(sroot, portdir, true);
	if (ret != NULL) {
		char buf[_Q_PATH_MAX];
		char *repo = NULL;
		size_t repolen = 0;

		snprintf(buf, sizeof(buf), "%s%s/%s", sroot, portdir, portrepo_name);
		if (eat_file(buf, &repo, &repolen)) {
			(void)rmspace(repo);
			ret->repo = repo;
		}
		ret->treetype = TREE_EBUILD;
	}
	return ret;
}

static const char binpkg_packages[]  = "Packages";
tree_ctx *
tree_open_binpkg(const char *sroot, const char *spkg)
{
	tree_ctx *ret = tree_open_int(sroot, spkg, true);
	int fd;

	if (ret != NULL) {
		ret->treetype = TREE_BINPKGS;

		fd = openat(ret->tree_fd, binpkg_packages, O_RDONLY | O_CLOEXEC);
		if (eat_file_fd(fd, &ret->cache.store, &ret->cache.storesize)) {
			ret->treetype = TREE_PACKAGES;
		} else if (ret->cache.store != NULL) {
			free(ret->cache.store);
			ret->cache.store = NULL;
		}
		close(fd);
	}

	return ret;
}

#ifdef ENABLE_GTREE
tree_ctx *
tree_open_gtree_int(const char *sroot, const char *tar, bool quiet)
{
	tree_ctx             *ctx = xzalloc(sizeof(*ctx));
	struct archive       *gt;
	struct archive_entry *entry;

	ctx->portroot_fd = open(sroot, O_RDONLY | O_CLOEXEC | O_PATH);
	if (ctx->portroot_fd == -1) {
		if (!quiet)
			warnp("could not open root: %s", sroot);
		free(ctx);
		return NULL;
	}

	/* Skip the leading slash */
	tar++;
	if (*tar == '\0')
		tar = ".";
	ctx->tree_fd = openat(ctx->portroot_fd, tar, O_RDONLY | O_CLOEXEC);
	if (ctx->tree_fd == -1) {
		if (!quiet)
			warnp("could not open tree: %s (in root %s)", tar, sroot);
		close(ctx->portroot_fd);
		free(ctx);
		return NULL;
	}

	if (sroot[0] == '/' && sroot[1] == '\0')
		sroot = "";
	snprintf(ctx->path, sizeof(ctx->path), "%s/%s", sroot, tar);

	gt = archive_read_new();
	archive_read_support_format_all(gt);
	if (archive_read_open_fd(gt, ctx->tree_fd, BUFSIZ) != ARCHIVE_OK ||
		archive_read_next_header(gt, &entry) != ARCHIVE_OK)
	{
		if (!quiet)
			warnp("could not open tree: %s (in root %s): %s",
				  tar, sroot, archive_error_string(gt));
		archive_read_free(gt);
		close(ctx->portroot_fd);
		close(ctx->tree_fd);
		free(ctx);
		return NULL;
	}
	if (strcmp(archive_entry_pathname(entry), "gtree-1") != 0) {
		if (!quiet)
			warnp("could not open tree: %s (in root %s): not a gtree container",
				  tar, sroot);
		archive_read_free(gt);
		close(ctx->portroot_fd);
		close(ctx->tree_fd);
		free(ctx);
		return NULL;
	}
	/* defer repo for now */
	archive_read_free(gt);

	return ctx;
}
#endif

void
tree_close(tree_ctx *ctx)
{
	if (ctx->cache.categories != NULL) {
		DECLARE_ARRAY(t);
		size_t n;
		tree_cat_ctx *cat;

		values_set(ctx->cache.categories, t);
		free_set(ctx->cache.categories);
		ctx->cache.categories = NULL;  /* must happen before close_cat */

		array_for_each(t, n, cat) {
			int i;
			/* ensure we cleanup all pkgs */
			for (i = 0; i < cat->pkg_cnt; i++)
				cat->pkg_ctxs[i]->cached = false;
			cat->pkg_cur = 0;
			tree_close_cat(cat);
		}

		xarrayfree_int(t);
	}
	if (ctx->cache.store != NULL)
		free(ctx->cache.store);

	if (ctx->dir != NULL)
		closedir(ctx->dir);
	else if (ctx->tree_fd >= 0)
		close(ctx->tree_fd); /* closedir() above does this for us */
	close(ctx->portroot_fd);
	if (ctx->do_sort)
		scandir_free(ctx->cat_de, ctx->cat_cnt);
	if (ctx->repo != NULL)
		free(ctx->repo);
	if (ctx->pkgs != NULL)
		free(ctx->pkgs);
	if (ctx->ebuilddir_ctx != NULL)
		free(ctx->ebuilddir_ctx);
	if (ctx->subtree != NULL)
		tree_close(ctx->subtree);
	free(ctx);
}

static int
tree_filter_cat(const struct dirent *de)
{
	int i;
	bool founddash;

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

tree_cat_ctx *
tree_open_cat(tree_ctx *ctx, const char *name)
{
	tree_cat_ctx *cat_ctx;
	int fd;
	DIR *dir;

	/* lookup in the cache, if any */
	if (ctx->cache.categories != NULL) {
		cat_ctx = get_set(name, ctx->cache.categories);
		if (cat_ctx != NULL) {
			/* reset state so it can be re-iterated (sort benefits the
			 * most here) */
			if (ctx->do_sort) {
				cat_ctx->pkg_cur = 0;
			} else {
				rewinddir(cat_ctx->dir);
			}
			return cat_ctx;
		}
	}

	/* Cannot use O_PATH as we want to use fdopendir() */
	fd = openat(ctx->tree_fd, name, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return NULL;

	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return NULL;
	}

	cat_ctx = xzalloc(sizeof(*cat_ctx));
	cat_ctx->name = name;
	cat_ctx->fd = fd;
	cat_ctx->dir = dir;
	cat_ctx->ctx = ctx;
	cat_ctx->pkg_ctxs = NULL;
	cat_ctx->pkg_cur = 0;
	cat_ctx->pkg_cnt = 0;

	if (ctx->cache.categories != NULL) {
		ctx->cache.categories =
			add_set_value(name, cat_ctx, NULL, ctx->cache.categories);
		/* ensure name doesn't expire after this instantiation is closed */
		cat_ctx->name = contains_set(name, ctx->cache.categories);
	}

	return cat_ctx;
}

static tree_cat_ctx *
tree_next_cat(tree_ctx *ctx)
{
	/* search for a category directory */
	tree_cat_ctx *cat_ctx = NULL;
	const struct dirent *de;

	if (ctx->do_sort) {
		if (ctx->cat_de == NULL) {
			if (ctx->cache.all_categories) {
				char **cats;
				size_t i;
				size_t len;
				struct dirent **ret;

				/* exploit the cache instead of reading from directory */
				ctx->cat_cnt = cnt_set(ctx->cache.categories);
				ctx->cat_de = ret = xmalloc(sizeof(*ret) * ctx->cat_cnt);
				list_set(ctx->cache.categories, &cats);
				for (i = 0; i < ctx->cat_cnt; i++) {
					len = strlen(cats[i]) + 1;
					ret[i] = xzalloc(sizeof(*de) + len);
					snprintf(ret[i]->d_name, len, "%s", cats[i]);
				}
				if (i > 1)
					qsort(ret, ctx->cat_cnt, sizeof(*ret),
							(int (*)(const void *, const void *))alphasort);
				free(cats);
			} else {
				int sdret = scandirat(ctx->tree_fd, ".",
									  &ctx->cat_de,
									  tree_filter_cat, alphasort);
				/* CID 548426 */
				if (sdret < 0)
					ctx->cat_cnt = 0;
				else
					ctx->cat_cnt = (size_t)sdret;
			}
			ctx->cat_cur = 0;
		}

		while (ctx->cat_cur < ctx->cat_cnt) {
			de = ctx->cat_de[ctx->cat_cur++];

			/* match if cat is requested */
			if (ctx->query_atom != NULL && ctx->query_atom->CATEGORY != NULL &&
					strcmp(ctx->query_atom->CATEGORY, de->d_name) != 0)
				continue;

			cat_ctx = tree_open_cat(ctx, de->d_name);
			if (!cat_ctx)
				continue;

			break;
		}
	} else {
		/* cheaper "streaming" variant */
		do {
			de = readdir(ctx->dir);
			if (!de)
				break;

			if (tree_filter_cat(de) == 0)
				continue;

			/* match if cat is requested */
			if (ctx->query_atom != NULL && ctx->query_atom->CATEGORY != NULL &&
					strcmp(ctx->query_atom->CATEGORY, de->d_name) != 0)
				continue;

			cat_ctx = tree_open_cat(ctx, de->d_name);
			if (!cat_ctx)
				continue;

			break;
		} while (1);
	}

	return cat_ctx;
}

void
tree_close_cat(tree_cat_ctx *cat_ctx)
{
	if (cat_ctx->ctx->cache.categories != NULL &&
		(cat_ctx->ctx->cache.all_categories ||
		 contains_set(cat_ctx->name, cat_ctx->ctx->cache.categories)))
		return;

	/* cleanup unreturned pkgs when sorted (or cache in use) */
	while (cat_ctx->pkg_cur < cat_ctx->pkg_cnt)
		tree_close_pkg(cat_ctx->pkg_ctxs[cat_ctx->pkg_cur++]);

	closedir(cat_ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->fd); */
	if (cat_ctx->ctx->do_sort)
		free(cat_ctx->pkg_ctxs);

	free(cat_ctx);
}

static int
tree_filter_pkg(const struct dirent *de)
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

	if (i > 0 &&
		(strcmp(de->d_name, "Manifest") == 0 ||
		 strcmp(de->d_name, "metadata.xml") == 0))
		i = 0;

	return i;
}

tree_pkg_ctx *
tree_open_pkg(tree_cat_ctx *cat_ctx, const char *name)
{
	tree_pkg_ctx *pkg_ctx;
	bool          isgpkg  = false;
	depend_atom  *patom   = NULL;

	if (cat_ctx->ctx->treetype == TREE_EBUILD &&
		cat_ctx->ctx->ebuilddir_cat_ctx == cat_ctx)
	{
		size_t len = strlen(name);
		char  *p   = (char *)&name[len - (sizeof(".ebuild") - 1)];
		if (len <= sizeof(".ebuild") - 1 ||
			memcmp(p, ".ebuild", sizeof(".ebuild") - 1) != 0)
			return NULL;  /* invalid, must be some random other file */
		*p = '\0';
	} else if (cat_ctx->ctx->treetype == TREE_BINPKGS) {
		size_t len   = strlen(name);
		char  *p     = (char *)&name[len - (sizeof(".gpkg.tar") - 1)];
		bool   isxpk = false;

		/* we pre-build the atom here to pick up on the build_id in the
		 * filename, if any */

		if (len > sizeof(".gpkg.tar") - 1 &&
			memcmp(p, ".gpkg.tar", sizeof(".gpkg.tar") - 1) == 0)
		{
			isgpkg = true;
			patom  = atom_explode_cat(name, cat_ctx->name);
			*p     = '\0';
		}

		if (!isgpkg &&
			(len > sizeof(".tbz2") - 1 &&
			 (p = (char *)&name[len - (sizeof(".tbz2") - 1)]) != NULL &&
			 (memcmp(p, ".tbz2", sizeof(".tbz2") - 1) == 0 ||
			  memcmp(p, ".xpak", sizeof(".xpak") - 1) == 0)))
		{
			isxpk = true;
			patom  = atom_explode_cat(name, cat_ctx->name);
			*p    = '\0';
		}

		if (!isgpkg &&
			!isxpk)
		{
			struct stat sb;

			if (cat_ctx->ctx->ebuilddir_cat_ctx == cat_ctx)
				return NULL;  /* invalid, like above but only as leaf */

			/* only accept this one if it is a directory (PN) */
			if (fstatat(cat_ctx->fd, name, &sb, 0) < 0)
				return NULL;  /* nothing? */
			if (!S_ISDIR(sb.st_mode))
				return NULL;  /* invalid random file */
		}
	}

	pkg_ctx                = xzalloc(sizeof(*pkg_ctx));
	pkg_ctx->name          = name;
	pkg_ctx->repo          = cat_ctx->ctx->repo;
	pkg_ctx->fd            = -1;
	pkg_ctx->cat_ctx       = cat_ctx;
	pkg_ctx->binpkg_isgpkg = isgpkg;
	pkg_ctx->atom          = patom;

	/* see if this pkg matches the query, here we can finally check
	 * version conditions like >=, etc. */
	if (cat_ctx->ctx->query_atom != NULL) {
		(void)tree_get_atom(pkg_ctx, cat_ctx->ctx->query_atom->SLOT != NULL);
		if (atom_compare(pkg_ctx->atom, cat_ctx->ctx->query_atom) != EQUAL) {
			pkg_ctx->name = NULL;
			tree_close_pkg(pkg_ctx);
			return NULL;
		}
	}

	return pkg_ctx;
}

static int
tree_pkg_compar(const void *l, const void *r)
{
	tree_pkg_ctx *pl = *(tree_pkg_ctx **)l;
	tree_pkg_ctx *pr = *(tree_pkg_ctx **)r;
	depend_atom *al = tree_get_atom(pl, false);
	depend_atom *ar = tree_get_atom(pr, false);

	return atom_compar_cb(&al, &ar);
}

static tree_pkg_ctx *
tree_next_pkg_int(tree_cat_ctx *cat_ctx)
{
	tree_pkg_ctx *pkg_ctx = NULL;
	const struct dirent *de;
	const depend_atom *qa = cat_ctx->ctx->query_atom;

	if (cat_ctx->ctx->do_sort) {
		if (cat_ctx->pkg_ctxs == NULL) {
			size_t pkg_size = 0;
			cat_ctx->pkg_cnt = 0;
			cat_ctx->pkg_cur = 0;
			while ((de = readdir(cat_ctx->dir)) != NULL) {
				char *name;

				if (tree_filter_pkg(de) == 0)
					continue;

				/* perform package name check, for we don't have an atom
				 * yet, and creating it is expensive, which we better
				 * defer to pkg time, and filter most stuff out here
				 * note that we might over-match, but that's easier than
				 * trying to deal with end of string or '-' here (which
				 * still wouldn't be 100% because name rules are complex) */
				if (qa != NULL && qa->PN != NULL &&
						strncmp(qa->PN, de->d_name, strlen(qa->PN)) != 0)
					continue;

				if (cat_ctx->pkg_cnt == pkg_size) {
					pkg_size += 256;
					cat_ctx->pkg_ctxs = xrealloc(cat_ctx->pkg_ctxs,
								sizeof(*cat_ctx->pkg_ctxs) * pkg_size);
				}

				name = xstrdup(de->d_name);
				pkg_ctx = cat_ctx->pkg_ctxs[cat_ctx->pkg_cnt++] =
					tree_open_pkg(cat_ctx, name);
				if (pkg_ctx == NULL) {
					free(name);
					cat_ctx->pkg_cnt--;
				}
			}

			if (cat_ctx->pkg_cnt > 1) {
				qsort(cat_ctx->pkg_ctxs, cat_ctx->pkg_cnt,
						sizeof(*cat_ctx->pkg_ctxs), tree_pkg_compar);
			}
		}

		pkg_ctx = NULL;
		if (cat_ctx->pkg_cur < cat_ctx->pkg_cnt)
			pkg_ctx = cat_ctx->pkg_ctxs[cat_ctx->pkg_cur++];
	} else {
		do {
			de = readdir(cat_ctx->dir);
			if (!de)
				break;

			if (tree_filter_pkg(de) == 0)
				continue;

			/* perform package name check as for the sorted variant */
			if (qa != NULL && qa->PN != NULL &&
					strncmp(qa->PN, de->d_name, strlen(qa->PN)) != 0)
				continue;

			pkg_ctx = tree_open_pkg(cat_ctx, de->d_name);
			if (!pkg_ctx)
				continue;

			break;
		} while (1);
	}

	return pkg_ctx;
}

tree_pkg_ctx *
tree_next_pkg(tree_cat_ctx *cat_ctx)
{
	tree_ctx     *ctx     = cat_ctx->ctx;
	tree_pkg_ctx *ret     = NULL;
	bool          recurse = false;
	bool          dofile  = false;

	/* first resume an on-going recursed listing */
	if (ctx->ebuilddir_pkg_ctx != NULL) {
		ret = tree_next_pkg_int(ctx->ebuilddir_cat_ctx);
		if (ret == NULL) {
			tree_close_cat(ctx->ebuilddir_cat_ctx);
			if (!cat_ctx->ctx->do_sort ||
				cat_ctx->pkg_ctxs == NULL)
				tree_close_pkg(ctx->ebuilddir_pkg_ctx);
			ctx->ebuilddir_pkg_ctx = NULL;
		} else {
			ret->binpkg_ismulti = true;  /* no matter if this is ebuild */
			return ret;
		}
	}

	/* load next entry */
	ret = tree_next_pkg_int(cat_ctx);
	if (ret == NULL)
		return ret;

	if (ctx->treetype == TREE_EBUILD) {
		/* serve *.ebuild files each as separate pkg_ctx with name set
		 * to CAT/P like in VDB and metadata, this is the only supported
		 * format, so if there's a non-directory in here, we just ignore
		 * it, which is fine */
		recurse = true;
	} else if (ctx->treetype == TREE_BINPKGS) {
		/* with FEATURES=binpkg-multi-instance we can have at worst a
		 * mix here of files and directories, so we must handle both */
		recurse = true;
		dofile  = true;
	}

	if (recurse) {
		tree_ctx *pkgdir = ctx->ebuilddir_ctx;

		if (pkgdir == NULL)
			pkgdir = ctx->ebuilddir_ctx = xmalloc(sizeof(*pkgdir));

		ctx->ebuilddir_cat_ctx = NULL;
		ctx->ebuilddir_pkg_ctx = ret;

		memset(pkgdir, 0, sizeof(*pkgdir));
		pkgdir->portroot_fd = -1;
		pkgdir->tree_fd     = cat_ctx->fd;
		pkgdir->do_sort     = ctx->do_sort;
		pkgdir->repo        = ctx->repo;
		pkgdir->treetype    = ctx->treetype;
		pkgdir->subtree     = ctx->subtree;

		ctx->ebuilddir_cat_ctx =
			tree_open_cat(pkgdir, ctx->ebuilddir_pkg_ctx->name);

		/* opening might fail if what we found wasn't a
		 * directory or something */
		if (ctx->ebuilddir_cat_ctx == NULL) {
			ctx->ebuilddir_pkg_ctx = NULL;
			if (dofile)
				return ret;
			else
				tree_close_pkg(ret);
		} else {
			/* "zap" the pkg such that it looks like CAT/P */
			ctx->ebuilddir_cat_ctx->name = cat_ctx->name;
			ctx->ebuilddir_cat_ctx->ctx  = ctx;
		}

		/* recurse to get the next step */
		ret = tree_next_pkg(cat_ctx);
	}

	return ret;
}

static int
tree_pkg_vdb_openat(
		tree_pkg_ctx *pkg_ctx,
		const char *file,
		int flags, mode_t mode)
{
	if (pkg_ctx->fd == -1) {
		pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
				O_RDONLY | O_CLOEXEC | O_PATH);
		if (pkg_ctx->fd == -1)
			return -1;
	}

	return openat(pkg_ctx->fd, file, flags | O_CLOEXEC, mode);
}

static bool
tree_pkg_vdb_eat(
		tree_pkg_ctx *pkg_ctx,
		const char *file,
		char **bufptr,
		size_t *buflen)
{
	int fd;
	bool ret;

	fd = tree_pkg_vdb_openat(pkg_ctx, file, O_RDONLY, 0);
	ret = eat_file_fd(fd, bufptr, buflen);
	if (ret)
		rmspace(*bufptr);

	if (fd != -1)
		close(fd);
	return ret;
}

#define tree_meta_alloc_storage(M,SIZ) { \
	struct tree_pkg_meta_ll *blk; \
	size_t                   newlen; \
\
	/* calculate new block size, ensuring it covers whatever we \
	 * need to write this iteration */ \
	newlen     = ((((SIZ) + 1) / BUFSIZ) + 1) * BUFSIZ; \
	blk        = xmalloc(sizeof(*blk) + newlen); \
	memset(blk, 0, sizeof(*blk)); \
	blk->next  = M->storage; \
	blk->ptr   = (char *)blk + sizeof(*blk); \
	blk->len   = newlen; \
	M->storage = blk; \
}

static tree_pkg_meta *
tree_read_file_pms(tree_pkg_ctx *pkg_ctx)
{
	struct stat s;
	char *ptr;
	FILE *f;
	tree_pkg_meta *ret = NULL;
	size_t len;

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xmalloc(len);
	memset(ret, 0, sizeof(*ret));
	ptr = (char *)ret + sizeof(*ret);
	if ((off_t)fread(ptr, 1, s.st_size, f) != s.st_size)
		goto err;
	ptr[s.st_size] = '\0';

	ret->Q_DEPEND = ptr;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->Q_##curr, '\n')) == NULL) { \
		warn("Invalid cache file for '%s/%s'", \
			 pkg_ctx->cat_ctx->name, pkg_ctx->name); \
		goto err; \
	} \
	ret->Q_##next = ptr+1; \
	*ptr = '\0';
	next_line(DEPEND, RDEPEND)
	next_line(RDEPEND, SLOT)
	next_line(SLOT, SRC_URI)
	next_line(SRC_URI, RESTRICT)
	next_line(RESTRICT, HOMEPAGE)
	next_line(HOMEPAGE, LICENSE)
	next_line(LICENSE, DESCRIPTION)
	next_line(DESCRIPTION, KEYWORDS)
	next_line(KEYWORDS, INHERITED)
	next_line(INHERITED, IUSE)
	next_line(IUSE, CDEPEND)
	next_line(CDEPEND, PDEPEND)
	next_line(PDEPEND, PROVIDE)
	next_line(PROVIDE, EAPI)
	next_line(EAPI, PROPERTIES)
#undef next_line
	ptr = strchr(ptr+1, '\n');
	if (ptr == NULL) {
		warn("Invalid cache file for '%s/%s' - could not find end of cache data",
			 pkg_ctx->cat_ctx->name, pkg_ctx->name);
		goto err;
	}
	*ptr = '\0';

	fclose(f);
	pkg_ctx->fd = -1;

	return ret;

err:
	if (f)
		fclose(f);
	pkg_ctx->fd = -1;
	if (ret)
		tree_close_meta(ret);
	return NULL;
}

static tree_pkg_meta *
tree_read_file_md5(tree_pkg_ctx *pkg_ctx)
{
	struct stat s;
	char *ptr, *endptr;
	FILE *f;
	tree_pkg_meta *ret = NULL;
	size_t len;

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xmalloc(len);
	memset(ret, 0, sizeof(*ret));
	ptr = (char *)ret + sizeof(*ret);
	if ((off_t)fread(ptr, 1, s.st_size, f) != s.st_size)
		goto err;
	ptr[s.st_size] = '\0';

	/* We have a block of key=value\n data.
	 * KEY=VALUE\n
	 * Where KEY does NOT contain:
	 * \0 \n =
	 * And VALUE does NOT contain:
	 * \0 \n
	 * */
#define assign_var_cmp(keyname, cmpkey) \
	if (strncmp(keyptr, cmpkey, strlen(cmpkey)) == 0) { \
		ret->Q_##keyname = valptr; \
		continue; \
	}
#define assign_var(keyname) \
	assign_var_cmp(keyname, #keyname);

	endptr = strchr(ptr, '\0');
	if (endptr == NULL) {
			warn("Invalid cache file for '%s/%s': "
					"could not find end of cache data",
					pkg_ctx->cat_ctx->name, pkg_ctx->name);
			goto err;
	}

	while (ptr != NULL && ptr != endptr) {
		char *keyptr;
		char *valptr;
		keyptr = ptr;
		valptr = strchr(ptr, '=');
		if (valptr == NULL) {
			warn("Invalid cache file for '%s/%s': missing val",
					pkg_ctx->cat_ctx->name, pkg_ctx->name);
			goto err;
		}
		*valptr = '\0';
		valptr++;
		ptr = strchr(valptr, '\n');
		if (ptr == NULL) {
			warn("Invalid cache file for '%s/%s': missing key",
					pkg_ctx->cat_ctx->name, pkg_ctx->name);
			goto err;
		}
		*ptr = '\0';
		ptr++;

		assign_var(CDEPEND);
		assign_var(DEPEND);
		assign_var(DESCRIPTION);
		assign_var(EAPI);
		assign_var(HOMEPAGE);
		assign_var(INHERITED);
		assign_var(IUSE);
		assign_var(KEYWORDS);
		assign_var(LICENSE);
		assign_var(PDEPEND);
		assign_var(PROPERTIES);
		assign_var(PROVIDE);
		assign_var(RDEPEND);
		assign_var(RESTRICT);
		assign_var(SLOT);
		assign_var(SRC_URI);
		assign_var(DEFINED_PHASES);
		assign_var(REQUIRED_USE);
		assign_var(BDEPEND);
		assign_var(IDEPEND);
		assign_var(EPREFIX);
		assign_var(_eclasses_);
		assign_var(_md5_);
		IF_DEBUG(warn("Cache file for '%s/%s' has unknown key %s",
					pkg_ctx->cat_ctx->name, pkg_ctx->name, keyptr));
	}
#undef assign_var
#undef assign_var_cmp

	fclose(f);
	pkg_ctx->fd = -1;

	return ret;

err:
	if (f)
		fclose(f);
	pkg_ctx->fd = -1;
	if (ret)
		tree_close_meta(ret);
	return NULL;
}

static tree_pkg_meta *
tree_read_file_ebuild(tree_pkg_ctx *pkg_ctx)
{
	FILE *f;
	struct stat s;
	tree_pkg_meta *ret = NULL;
	size_t len;
	char *p;
	char *q;
	char *w;
	char **key;
	bool esc;
	bool findnl;

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xmalloc(len);
	memset(ret, 0, sizeof(*ret));
	p = (char *)ret + sizeof(*ret);
	if ((off_t)fread(p, 1, s.st_size, f) != s.st_size)
		goto err;
	p[s.st_size] = '\0';

	do {
		q = p;
		while (*p >= 'A' && *p <= 'Z')
			p++;

		key = NULL;
		if (q < p && *p == '=') {
			*p++ = '\0';
			/* match variable against which ones we look for */
#define match_key(X) else if (strcmp(q, #X) == 0) key = &ret->Q_##X
			if (1 == 0); /* dummy for syntax */
			match_key(DEPEND);
			match_key(RDEPEND);
			match_key(SLOT);
			match_key(SRC_URI);
			match_key(RESTRICT);
			match_key(HOMEPAGE);
			match_key(LICENSE);
			match_key(DESCRIPTION);
			match_key(KEYWORDS);
			match_key(IUSE);
			match_key(CDEPEND);
			match_key(PDEPEND);
			match_key(EAPI);
			match_key(REQUIRED_USE);
			match_key(BDEPEND);
			match_key(IDEPEND);
#undef match_key
		}

		findnl = true;
		if (key != NULL) {
			q = p;
			if (*q == '"' || *q == '\'') {
				/* find matching quote */
				p++;
				w = p;
				esc = false;
				do {
					while (*p != '\0' && *p != *q) {
						if (*p == '\\') {
							esc = !esc;
							if (esc) {
								p++;
								continue;
							}
						} else {
							/* stash everything on a single line like
							 * VDB and md5-cache do */
							if (*p == '\n' || *p == '\r')
								*p = ' ';
							esc = false;
						}

						/* collapse sequences of spaces */
						if (*w != ' ' || *p != ' ')
							*w++ = *p++;
						else
							p++;
					}
					if (*p == *q && esc) {
						/* escaped, move along */
						esc = false;
						*w++ = *p++;
						continue;
					}
					break;
				} while (1);
				q++;
				*w = '\0';
			} else {
				/* find first whitespace */
				while (!isspace((int)*p))
					p++;
				if (*p == '\n')
					findnl = false;
			}
			*p++ = '\0';
			*key = q;
		}

		if (findnl && (p = strchr(p, '\n')) != NULL)
			p++;
	} while (p != NULL);

	fclose(f);
	pkg_ctx->fd = -1;

	return ret;

err:
	if (f)
		fclose(f);
	pkg_ctx->fd = -1;
	if (ret)
		tree_close_meta(ret);
	return NULL;
}

static void
tree_read_file_binpkg_xpak_cb(
	void *ctx,
	char *pathname,
	int   pathname_len,
	int   data_offset,
	int   data_len,
	char *data)
{
	tree_pkg_meta *m = (tree_pkg_meta *)ctx;
	char         **key;
	size_t         pos;
	size_t         len;

#define match_path(K) \
	else if (pathname_len == (sizeof(#K) - 1) && strcmp(pathname, #K) == 0) \
		key = &m->Q_##K
	if (1 == 0); /* dummy for syntax */
	match_path(DEPEND);
	match_path(RDEPEND);
	match_path(SLOT);
	match_path(SRC_URI);
	match_path(RESTRICT);
	match_path(HOMEPAGE);
	match_path(DESCRIPTION);
	match_path(KEYWORDS);
	match_path(INHERITED);
	match_path(IUSE);
	match_path(CDEPEND);
	match_path(PDEPEND);
	match_path(PROVIDE);
	match_path(EAPI);
	match_path(PROPERTIES);
	match_path(DEFINED_PHASES);
	match_path(REQUIRED_USE);
	match_path(BDEPEND);
	match_path(IDEPEND);
	match_path(CONTENTS);
	match_path(USE);
	match_path(EPREFIX);
	match_path(repository);
	else
		return;
#undef match_path

	/* get current storage block */
	if (m->storage != NULL) {
		pos = m->storage->pos;
		len = m->storage->len;
	} else {
		pos = 0;
		len = 0;
	}

	/* trim whitespace (mostly trailing newline) */
	while (isspace((int)data[data_offset + data_len - 1]))
		data_len--;

	if (len - pos < (size_t)(data_len + 1)) {
		tree_meta_alloc_storage(m, data_len + 1);
		len = m->storage->len;
		pos = m->storage->pos;
	}

	*key = m->storage->ptr + pos;
	snprintf(*key, len - pos, "%.*s", data_len, data + data_offset);
	m->storage->pos += data_len + 1;
}

static tree_pkg_meta *
tree_read_file_binpkg(tree_pkg_ctx *pkg_ctx)
{
	tree_pkg_meta *m = xzalloc(sizeof(tree_pkg_meta));
	int newfd = -1;

	if (pkg_ctx->binpkg_isgpkg) {
#ifdef ENABLE_GPKG
		struct archive       *a     = archive_read_new();
		struct archive_entry *entry;
		size_t                len   = 0;
		char                 *buf   = NULL;

		archive_read_support_format_all(a);
		archive_read_support_filter_all(a);

		if (archive_read_open_fd(a, pkg_ctx->fd, BUFSIZ) != ARCHIVE_OK)
			return NULL;
		while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
			const char *pathname = archive_entry_pathname(entry);
			const char *fname    = strchr(pathname, '/');
			if (fname == NULL)
				continue;
			fname++;
			if (strncmp(fname, "metadata.tar",
						sizeof("metadata.tar") - 1) == 0)
			{
				/* read this nested tar, it contains the VDB entries
				 * otherwise stored in xpak */
				len = archive_entry_size(entry);
				buf = xmalloc(len);
				archive_read_data(a, buf, len);
				break;
			}
		}
		archive_read_free(a);
		newfd = pkg_ctx->fd;
		pkg_ctx->fd = -1;  /* will be closed by hash_multiple_fd */

		if (buf != NULL)
		{
			char  *data      = NULL;
			size_t data_size = 0;
			size_t data_len  = 0;

			a = archive_read_new();
			archive_read_support_format_all(a);
			archive_read_support_filter_all(a);
			archive_read_open_memory(a, buf, len);

			while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
				const char *pathname = archive_entry_pathname(entry);
				char       *fname    = strchr(pathname, '/');
				if (fname == NULL)
					continue;
				fname++;

				data_len = archive_entry_size(entry);
				if (data_len > data_size) {
					data_size = data_len;
					data      = xrealloc(data, data_size);
				}
				if (archive_read_data(a, data, data_len) < 0)
					continue;
				tree_read_file_binpkg_xpak_cb(m,
											  fname, (int)strlen(fname),
											  0, data_len, data);
			}
			archive_read_free(a);
			free(buf);
			free(data);
		}
#else
		return NULL;
#endif
	} else {
		newfd = dup(pkg_ctx->fd);
		xpak_process_fd(pkg_ctx->fd, true, m, tree_read_file_binpkg_xpak_cb);
		pkg_ctx->fd = -1;  /* closed by xpak_process_fd */
	}

	/* fill in some properties which are not available, but would be in
	 * Packages, and used to verify the package ... this is somewhat
	 * fake, but allows to transparantly use a dir of binpkgs */
	if (newfd != -1) {
		size_t fsize;
		size_t needlen = SHA1_DIGEST_LENGTH + 1 + 19 + 1;
		size_t pos = 0;
		size_t len = 0;

		if (m->storage != NULL) {
			pos = m->storage->pos;
			len = m->storage->len;
		}

		if (len - pos < needlen) {
			tree_meta_alloc_storage(m, needlen);
			len = m->storage->len;
			pos = m->storage->pos;
		}

		m->Q_SHA1 = m->storage->ptr + pos;
		m->Q_SIZE = m->Q_SHA1 + SHA1_DIGEST_LENGTH + 1;
		m->storage->pos += needlen;

		lseek(newfd, 0, SEEK_SET);  /* reposition at the start of file */
		if (hash_multiple_file_fd(newfd, NULL, m->Q_SHA1, NULL, NULL,
				NULL, &fsize, HASH_SHA1) == 0)
			snprintf(m->Q_SIZE, 19 + 1, "%zu", fsize);
	}

	return m;
}

static int
tree_pkg_read_openfd_int(tree_pkg_ctx *pkg_ctx)
{
	tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;

	if (pkg_ctx->fd == -1) {
		if (ctx->treetype == TREE_EBUILD ||
			ctx->treetype == TREE_BINPKGS)
		{
			char buf[_Q_PATH_MAX];
			snprintf(buf, sizeof(buf), "%s.%s", pkg_ctx->name,
					 ctx->treetype == TREE_EBUILD ? "ebuild" :
					 pkg_ctx->binpkg_isgpkg ? "gpkg.tar" :
					 pkg_ctx->binpkg_ismulti ? "xpak" : "tbz2");
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, buf,
								 O_RDONLY | O_CLOEXEC);
		} else {
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
								 O_RDONLY | O_CLOEXEC);
		}
	}

	return pkg_ctx->fd;
}

static tree_pkg_meta *
tree_pkg_read(tree_pkg_ctx *pkg_ctx)
{
	tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;
	tree_pkg_meta *ret = NULL;

	if (pkg_ctx->meta != NULL)
		return pkg_ctx->meta;

	if (tree_pkg_read_openfd_int(pkg_ctx) == -1)
		return NULL;

	if (ctx->treetype == TREE_METADATA_MD5) {
		ret = tree_read_file_md5(pkg_ctx);
		/* md5-cache, is sort of documented in egencache man-page
		 * key-points are that an md5 is provided for the ebuild itself,
		 * and if it includes eclasses, the md5s for each eclass.  These
		 * are available as _md5_ and _eclasses_ keys.  The latter uses
		 * tab-separation of form <eclass-name>\t<md5>\t... */
	} else if (ctx->treetype == TREE_METADATA_PMS) {
		ret = tree_read_file_pms(pkg_ctx);
		/* PMS implies to do an mtime and existence check (the cache may
		 * contain extra stuff) but since this form of metadata in fact
		 * is extinct, because these checks are insufficient and
		 * impossible on e.g. a git-based tree. */
	} else if (ctx->treetype == TREE_EBUILD) {
		ret = NULL;
		if (ctx->subtree != NULL) {
			tree_cat_ctx  *scat;
			tree_pkg_ctx  *spkg  = NULL;

			/* the cache takes care of repeated calls here */
			scat = tree_open_cat(ctx->subtree, pkg_ctx->cat_ctx->name);
			if (scat != NULL)
				spkg = tree_open_pkg(scat, pkg_ctx->name);

			if (spkg != NULL) {
				if (ctx->subtree->treetype == TREE_METADATA_MD5) {
					/* in this case a cache entry exists, however, it
					 * may be out of date, for that we need to check the
					 * md5 hashes with the ebuild/eclass files,
					 * obviously when the source ebuild doesn't exist,
					 * we never get here */
					char   *mdmd5;
					char    srcmd5[MD5_DIGEST_LENGTH + 1];
					size_t  flen;

					if (hash_multiple_file_fd(pkg_ctx->fd,
											  srcmd5, NULL, NULL, NULL,
											  NULL, &flen, HASH_MD5) == 0)
						pkg_ctx->fd = -1;

					mdmd5 = tree_pkg_meta_get(spkg, _md5_);
					/* TODO: eclass compares */

					/* is this a valid cache? use it! */
					if (mdmd5 != NULL &&
						memcmp(mdmd5, srcmd5, MD5_DIGEST_LENGTH) == 0)
					{
						ret = tree_pkg_read(spkg);
					}
				} else if (ctx->subtree->treetype == TREE_METADATA_PMS) {
					struct stat ebld;
					struct stat pmsc;

					if (fstat(pkg_ctx->fd, &ebld) != 0 ||
						fstat(tree_pkg_read_openfd_int(spkg), &pmsc) != 0 ||
						ebld.st_mtime > pmsc.st_mtime ||
						(ebld.st_mtime == pmsc.st_mtime &&
						 ebld.st_mtim.tv_nsec > pmsc.st_mtim.tv_nsec))
					{
						/* fail or ebuild is newer, so ignore */
					} else {
						ret = tree_pkg_read(spkg);
					}
				}
				if (ret != NULL) {
					/* transplant meta to pkg, so we can free spkg */
					pkg_ctx->meta = spkg->meta;
					spkg->meta    = NULL;
				}
				tree_close_pkg(spkg);
			}
		}
		if (ret == NULL) {
			(void)tree_pkg_read_openfd_int(pkg_ctx);  /* re-open if fallback */
			ret = tree_read_file_ebuild(pkg_ctx);
		}
	} else if (ctx->treetype == TREE_BINPKGS) {
		ret = tree_read_file_binpkg(pkg_ctx);
	} else if (ctx->treetype == TREE_PACKAGES) {
		ret = (tree_pkg_meta *)pkg_ctx->cat_ctx->ctx->pkgs;
	}

	pkg_ctx->meta = ret;

	if (ret == NULL)
		warn("Unknown/unsupported metadata cache type!");
	return ret;
}

static tree_pkg_meta *
tree_clone_meta(tree_pkg_meta *m)
{
	tree_pkg_meta *ret;
	size_t         pos = 0;
	size_t         len = 0;
	char         **ptr;
	char          *p;

	/* compute necessary space upfront */
	len = sizeof(*ret);
	for (ptr = &m->Q__data; ptr <= &m->Q__last; ptr++)
		if (*ptr != NULL)
			len += strlen(*ptr) + 1;

	/* malloc and copy */
	ret = xzalloc(len);
	p = (char *)ret + sizeof(*ret);
	for (ptr = &m->Q__data; ptr <= &m->Q__last; ptr++, pos++) {
		if (*ptr == NULL)
			continue;
		*(&ret->Q__data + pos) = p;
		len = strlen(*ptr) + 1;
		memcpy(p, *ptr, len);
		p += len;
	}

	return ret;
}

static void
tree_close_meta(tree_pkg_meta *cache)
{
	struct tree_pkg_meta_ll *blk;

	if (cache == NULL)
		errf("Cache is empty !");
	while (cache->storage != NULL) {
		blk = cache->storage->next;
		free(cache->storage);
		cache->storage = blk;
	}
	free(cache);
}

char *
tree_pkg_meta_get_int(tree_pkg_ctx *pkg_ctx, size_t offset, const char *keyn)
{
	tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;
	char **key;

	/* offset is a byte offset in the tree_pkg_meta struct, pointing to
	 * key, the tree_pkg_meta_get macro as called by the user takes care
	 * of offset and keyn pointing to the same thing */

	if (ctx->treetype == TREE_VDB) {
		if (pkg_ctx->meta == NULL)
			pkg_ctx->meta = xzalloc(sizeof(tree_pkg_meta));

		key = (char **)((char *)&pkg_ctx->meta->Q__data + offset);

		/* just eat the file if we haven't yet */
		if (*key == NULL) {
			int fd = tree_pkg_vdb_openat(pkg_ctx, keyn, O_RDONLY, 0);
			struct stat s;
			size_t pos;
			size_t len;
			char *p;
			tree_pkg_meta *m = pkg_ctx->meta;

			if (fd < 0)
				return NULL;
			if (fstat(fd, &s) != 0) {
				close(fd);
				return NULL;
			}

			if (m->storage != NULL) {
				pos = m->storage->pos;
				len = m->storage->len;
			} else {
				pos = 0;
				len = 0;
			}

			if (len - pos < (size_t)(s.st_size + 1)) {
				tree_meta_alloc_storage(m, s.st_size + 1);
				pos = m->storage->pos;
				len = m->storage->len;
			}

			p = *key = m->storage->ptr + pos;
			if (read(fd, p, s.st_size) == (ssize_t)s.st_size) {
				p[s.st_size] = '\0';
				while (s.st_size > 0 && isspace((int)p[s.st_size - 1]))
					p[--s.st_size] = '\0';
				m->storage->pos += s.st_size + 1;
			}
			else
			{
				/* hmmm, couldn't read the whole file?!? */
				p[0] = '\0';
				m->storage->pos++;
			}
			close(fd);
		}
	} else {
		if (pkg_ctx->meta == NULL)
			pkg_ctx->meta = tree_pkg_read(pkg_ctx);
		if (pkg_ctx->meta == NULL)
			return NULL;

		key = (char **)((char *)&pkg_ctx->meta->Q__data + offset);

		/* Packages are nice, but also a bit daft, because they don't
		 * contain everything available (for a semi-good reason though)
		 * We cannot downgrade the tree execution to BINPKGS, because
		 * we're running from tree_foreach_packages */
		if (*key == NULL && ctx->treetype == TREE_PACKAGES) {
			ctx->treetype = TREE_BINPKGS;
			pkg_ctx->fd = -1;

			/* trigger tree_pkg_read to do something */
			if ((void *)pkg_ctx->meta != (void *)pkg_ctx->cat_ctx->ctx->pkgs)
				free(pkg_ctx->meta);
			pkg_ctx->meta = NULL;

			pkg_ctx->meta = tree_pkg_read(pkg_ctx);
			ctx->treetype = TREE_PACKAGES;
			if (pkg_ctx->meta == NULL) {
				/* hrmffff. */
				pkg_ctx->fd = -2;
				pkg_ctx->meta = tree_pkg_read(pkg_ctx);
			}
			key = (char **)((char *)&pkg_ctx->meta->Q__data + offset);
		}
	}
	return *key;
}

tree_metadata_xml *
tree_pkg_metadata(tree_pkg_ctx *pkg_ctx)
{
	tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;
	int fd;
	FILE *f;
	struct stat s;
	char *xbuf;
	char *p;
	char *q;
	size_t len;
	tree_metadata_xml *ret = NULL;
	struct elist *emailw = NULL;

	/* lame @$$ XML parsing, I don't want to pull in a real parser
	 * library because we only retrieve one element for now: email
	 * technically speaking, email may occur only once in a maintainer
	 * tag, but practically speaking we don't care at all, so we can
	 * just extract everything between <email> and </email> */

	if (ctx->treetype == TREE_EBUILD) {
		fd = openat(pkg_ctx->cat_ctx->fd, "metadata", O_RDONLY | O_CLOEXEC);
	} else {
		char buf[_Q_PATH_MAX];
		depend_atom *atom = tree_get_atom(pkg_ctx, false);
		snprintf(buf, sizeof(buf), "../../%s/%s/metadata.xml",
				atom->CATEGORY, atom->PN);
		fd = openat(ctx->tree_fd, buf, O_RDONLY | O_CLOEXEC);
	}

	if (fd == -1)
		return NULL;

	if ((f = fdopen(fd, "r")) == NULL) {
		close(fd);
		return NULL;
	}

	if (fstat(fd, &s) != 0) {
		fclose(f);
		return NULL;
	}

	len = sizeof(*ret) + s.st_size + 1;
	p = xbuf = xmalloc(len);
	if ((off_t)fread(p, 1, s.st_size, f) != s.st_size) {
		free(p);
		fclose(f);
		pkg_ctx->fd = -1;
		return NULL;
	}
	p[s.st_size] = '\0';

	ret = xmalloc(sizeof(*ret));
	ret->email = NULL;

	while ((q = strstr(p, "<email>")) != NULL) {
		p = q + sizeof("<email>") - 1;
		if ((q = strstr(p, "</email>")) == NULL)
			break;
		*q = '\0';
		rmspace(p);
		if (emailw == NULL) {
			emailw = ret->email = xmalloc(sizeof(*emailw));
		} else {
			emailw = emailw->next = xmalloc(sizeof(*emailw));
		}
		emailw->next = NULL;
		emailw->addr = xstrdup(p);
		p = q + 1;
	}

	free(xbuf);
	fclose(f);
	return ret;
}

void
tree_close_metadata(tree_metadata_xml *meta_ctx)
{
	struct elist *e;
	while (meta_ctx->email != NULL) {
		e = meta_ctx->email;
		free(e->addr);
		e = e->next;
		free(meta_ctx->email);
		meta_ctx->email = e;
	}
	free(meta_ctx);
}

void
tree_close_pkg(tree_pkg_ctx *pkg_ctx)
{
	if (pkg_ctx->cached)
		return;

	if (pkg_ctx->fd >= 0)
		close(pkg_ctx->fd);
	if (pkg_ctx->atom != NULL)
		atom_implode(pkg_ctx->atom);
	/* avoid freeing tree_ctx' repo */
	if (pkg_ctx->cat_ctx->ctx->repo != pkg_ctx->repo)
		free(pkg_ctx->repo);
	if (pkg_ctx->cat_ctx->ctx->do_sort)
		free((char *)pkg_ctx->name);
	free(pkg_ctx->slot);
	if (pkg_ctx->meta != NULL &&
			(void *)pkg_ctx->meta != (void *)pkg_ctx->cat_ctx->ctx->pkgs)
		tree_close_meta(pkg_ctx->meta);
	free(pkg_ctx);
}

static int
tree_foreach_cache_populate_cb
(
	tree_pkg_ctx *ctx,
	void         *priv
)
{
	tree_cat_ctx  *cat_ctx;
	tree_pkg_ctx  *pkg;
	set           *cache   = priv;
	tree_ctx      *tctx    = ctx->cat_ctx->ctx;
	depend_atom   *atom    = tree_get_atom(ctx, true);
	tree_pkg_meta *meta    = tree_pkg_read(ctx);

	cat_ctx = get_set(atom->CATEGORY, cache);
	if (cat_ctx == NULL) {
		cat_ctx = tree_open_cat(tctx, ".");
		if (cache != NULL)  /* for static code analysers */
			add_set_value(atom->CATEGORY, cat_ctx, NULL, cache);
		/* get a pointer from the set */
		cat_ctx->name = contains_set(atom->CATEGORY, cache);
	}

	pkg = xcalloc(1, sizeof(*pkg));

	/* intuitively this would feel like it could use a set, but since
	 * we're going to sort it, we need it as array anyway, so this is
	 * better, especially given that the tree should not be able to
	 * produce duplicates */
	cat_ctx->pkg_cnt++;
	if (cat_ctx->pkg_cnt > cat_ctx->pkg_siz) {
		cat_ctx->pkg_siz  = ((cat_ctx->pkg_cnt / 16) + 1) * 16;
		cat_ctx->pkg_ctxs =
			xrealloc(cat_ctx->pkg_ctxs,
					 sizeof(*cat_ctx->pkg_ctxs) * cat_ctx->pkg_siz);
	}
	cat_ctx->pkg_ctxs[cat_ctx->pkg_cnt - 1] = pkg;
	pkg->cached  = true;
	pkg->cat_ctx = cat_ctx;
	pkg->atom    = atom_clone(atom);
	pkg->name    = xstrdup(pkg->atom->PF);
	pkg->repo    = tctx->repo != NULL ? xstrdup(tctx->repo) : NULL;
	if (meta != NULL) {
		pkg->fd = -2;  /* don't try to read, we fill it in here */
		if (tctx->treetype == TREE_PACKAGES ||
			tctx->treetype == TREE_METADATA_GTREE)
		{
			/* need to copy, source is based on temp space in foreach */
			pkg->meta = tree_clone_meta(meta);
		} else if (tctx->treetype == TREE_BINPKGS) {
			/* BINPKG case, this one is read/allocated separately from
			 * xpak archive, so can just take it over */
			pkg->meta = meta;
			ctx->meta = NULL;  /* avoid double free */
		}
		pkg->binpkg_isgpkg = ctx->binpkg_isgpkg;
	} else {
		pkg->meta = NULL;
	}

	return 0;
}

static int
tree_foreach_packages(tree_ctx *ctx, tree_pkg_cb callback, void *priv)
{
	char              *p;
	char              *q;
	char              *c;
	char               pkgname[_Q_PATH_MAX];
	size_t             len;
	int                ret      = 0;
	const depend_atom *query    = ctx->query_atom;

	/* reused for every entry */
	tree_cat_ctx      *cat      = NULL;
	tree_pkg_ctx       pkg;
	tree_pkg_meta      meta;
	depend_atom       *atom     = NULL;

	/* re-read the contents, this is necessary to make it possible to
	 * call this function multiple times */
	if (ctx->cache.store == NULL || ctx->cache.store[0] == '\0') {
		int fd = openat(ctx->tree_fd, binpkg_packages, O_RDONLY | O_CLOEXEC);
		if (!eat_file_fd(fd, &ctx->cache.store, &ctx->cache.storesize)) {
			if (ctx->cache.store != NULL) {
				free(ctx->cache.store);
				ctx->cache.store = NULL;
			}
			close(fd);
			return 1;
		}
		close(fd);
	}

	p = ctx->cache.store;
	len = strlen(ctx->cache.store);  /* sucks, need eat_file change */

	memset(&meta, 0, sizeof(meta));

	do {
		/* find next line */
		c = NULL;
		for (q = p; len > 0 && *q != '\n'; q++, len--)
			if (c == NULL && *q == ':')
				c = q;

		if (len == 0)
			break;

		/* empty line, end of a block */
		if (p == q) {
			/* make callback with populated atom */
			if (atom != NULL) {
				size_t pkgnamelen;

				memset(&pkg, 0, sizeof(pkg));

				/* store meta ptr in ctx->pkgs, such that get_pkg_meta
				 * can grab it from there (for free) */
				ctx->pkgs = (char *)&meta;

				if (cat == NULL || strcmp(cat->name, atom->CATEGORY) != 0)
				{
					if (cat != NULL) {
						atom_implode((depend_atom *)cat->pkg_ctxs);
						cat->pkg_ctxs = NULL;
						tree_close_cat(cat);
					}
					cat = tree_open_cat(ctx, atom->CATEGORY);
					if (cat == NULL) {
						/* probably dir doesn't exist or something,
						 * generate a dummy cat */
						cat = tree_open_cat(ctx, ".");
					}
					cat->pkg_ctxs = (tree_pkg_ctx **)atom;  /* for name */
				}
				if (meta.Q_BUILDID != NULL) {
					atom->BUILDID = atoi(meta.Q_BUILDID);
				} else if (meta.Q_PATH != NULL) {
					depend_atom *patom;
					/* dreadful, but Portage apparently generates a
					 * Packages file without BUILD_ID while it does
					 * generate binpkgs with build-ids in them */
					c = strrchr(meta.Q_PATH, '/');
					patom = atom_explode(++c);
					if (patom != NULL) {
						atom->BUILDID = patom->BUILDID;
						atom_implode(patom);
					}
				}
				pkgnamelen = 0;
				if (meta.Q_PATH != NULL) {
					size_t plen = strlen(meta.Q_PATH);
					if (plen > sizeof(".tbz2") - 1 &&
						memcmp(meta.Q_PATH + plen - (sizeof(".tbz2") - 1),
							   ".tbz2", sizeof(".tbz2") - 1) == 0)
					{
						pkgnamelen = snprintf(pkgname, sizeof(pkgname),
											  "%s.tbz2", atom->PF);
						pkgname[pkgnamelen - (sizeof(".tbz2") - 1)] = '\0';
					} else if (plen > sizeof(".xpak") - 1 &&
							   memcmp(meta.Q_PATH + plen -
									  (sizeof(".xpak") - 1),
									  ".xpak", sizeof(".xpak") - 1) == 0)
					{
						pkgnamelen = snprintf(pkgname, sizeof(pkgname),
											  "%s.xpak", atom->PF);
						pkgname[pkgnamelen - (sizeof(".xpak") - 1)] = '\0';
					} else if (plen > sizeof(".gpkg.tar") - 1 &&
						memcmp(meta.Q_PATH + plen - (sizeof(".gpkg.tar") - 1),
							   ".gpkg.tar", sizeof(".gpkg.tar") - 1) == 0)
					{
						pkgnamelen = snprintf(pkgname, sizeof(pkgname),
											  "%s.gpkg.tar", atom->PF);
						pkgname[pkgnamelen - (sizeof(".gpkg.tar") - 1)] = '\0';
						pkg.binpkg_isgpkg = 1;
					}
				}
				if (pkgnamelen == 0) {
					pkgnamelen = snprintf(pkgname, sizeof(pkgname),
										  "%s", atom->PF);
				}
				pkg.name = pkgname;
				pkg.slot = meta.Q_SLOT == NULL ? (char *)"0" : meta.Q_SLOT;
				pkg.repo = ctx->repo;
				pkg.atom = atom;
				pkg.cat_ctx = cat;
				pkg.fd = -2;  /* intentional, meta has already been read */

				/* do call callback with pkg_atom (populate cat and pkg) */
				ret |= callback(&pkg, priv);

				ctx->pkgs = NULL;
				if (atom != (depend_atom *)cat->pkg_ctxs)
					atom_implode(atom);
			}

			memset(&meta, 0, sizeof(meta));
			atom = NULL;
			if (len > 0) {  /* hop over \n */
				p++;
				len--;
			}
			continue;
		}

		/* skip invalid lines */
		if (c == NULL || q - c < 3 || c[1] != ' ')
			continue;

		/* NULL-terminate p and c, file should end with \n */
		*q = '\0';
		*c = '\0';
		c += 2;         /* hop over ": " */
		if (len > 0) {  /* hop over \n */
			q++;
			len--;
		}

		if (strcmp(p, "REPO") == 0) { /* from global section in older files */
			ctx->repo = c;
		} else if (strcmp(p, "CPV") == 0) {
			if (atom != NULL)
				atom_implode(atom);
			atom = atom_explode(c);
			/* pretend this entry is bogus if it doesn't match query */
			if (query != NULL &&
				atom != NULL &&
				atom_compare(atom, query) != EQUAL)
			{
				atom_implode(atom);
				atom = NULL;
			}
#define match_key(X) match_key2(X,X)
#define match_key2(X,Y) \
		} else if (strcmp(p, #X) == 0) { \
			meta.Q_##Y = c
		match_key(DEFINED_PHASES);
		match_key(DEPEND);
		match_key2(DESC, DESCRIPTION);
		match_key(EAPI);
		match_key(IUSE);
		match_key(KEYWORDS);
		match_key(LICENSE);
		match_key(MD5);
		match_key(SHA1);
		match_key(RDEPEND);
		match_key(SLOT);
		match_key(USE);
		match_key(PDEPEND);
		match_key2(REPO, repository);
		match_key(SIZE);
		match_key(BDEPEND);
		match_key(IDEPEND);
		match_key(PATH);
		match_key2(BUILD_ID, BUILDID);
		match_key(BUILDID);
#undef match_key
#undef match_key2
		}

		p = q;
	} while (len > 0);

	if (cat != NULL) {
		atom_implode((depend_atom *)cat->pkg_ctxs);
		cat->pkg_ctxs = NULL;
		tree_close_cat(cat);
	}

	if (atom != NULL)
		atom_implode(atom);

	/* ensure we don't free a garbage pointer */
	ctx->repo = NULL;
	ctx->cache.store[0] = '\0';

	return ret;
}

#ifdef ENABLE_GTREE
struct tree_gtree_cb_ctx {
	struct archive *archive;
};
static la_ssize_t
tree_gtree_read_cb(struct archive *a, void *cctx, const void **buf)
{
	struct tree_gtree_cb_ctx *ctx = cctx;
	size_t                    size;
	la_int64_t                offset;  /* unused */
	int                       ret;

	(void)a;

	ret = archive_read_data_block(ctx->archive, buf, &size, &offset);
	if (ret == ARCHIVE_EOF)
		return 0;
	if (ret != ARCHIVE_OK)
		return -1;
	(void)offset;
	/* at this point I sincerely hope size is not going to be over the
	 * unsigned variant */
	return (la_ssize_t)size;
}
static int
tree_gtree_close_cb(struct archive *a, void *cctx)
{
	(void)a;
	(void)cctx;

	/* noop */
	return ARCHIVE_OK;
}

static int
tree_foreach_gtree(tree_ctx *ctx, tree_pkg_cb callback, void *priv)
{
	tree_cat_ctx             *cat         = NULL;
	tree_pkg_ctx              pkg;
	tree_pkg_meta             meta;
	depend_atom              *atom        = NULL;
	struct archive           *outer;
	struct archive           *inner;
	struct archive_entry     *entry;
	struct tree_gtree_cb_ctx  cb_ctx;
	char                     *p;
	const depend_atom        *query       = ctx->query_atom;
	bool                      foundcaches = false;
	size_t                    len;
	int                       ret         = 0;

	/* reused for every entry */
	cat       = xzalloc(sizeof(*cat));
	cat->ctx  = ctx;
	cat->name = "";

	/* rewind the outer tar, and re-read it, it's slight overhead that
	 * we have to re-read it entirely, but I cannot find an API to
	 * rewind or use an offset to re-read an entry again, and the
	 * overhead should be small given the outer is uncompressed and it's
	 * the second entry */
	lseek(ctx->tree_fd, 0, SEEK_SET);
	outer = archive_read_new();
	archive_read_support_format_all(outer);  /* don't see why not */
	if (archive_read_open_fd(outer, ctx->tree_fd, BUFSIZ) != ARCHIVE_OK) {
		warn("unable to read gtree container: %s",
			 archive_error_string(outer));
		archive_read_free(outer);
		return 1;
	}

	while (archive_read_next_header(outer, &entry) == ARCHIVE_OK) {
		const char *fname = archive_entry_pathname(entry);
		if (fname == NULL)
			continue;
		if (strncmp(fname, "repo.tar", sizeof("repo.tar") - 1) == 0 &&
			(fname[sizeof("repo.tar") - 1] == '.' ||
			 fname[sizeof("repo.tar") - 1] == '\0'))
			break;
		entry = NULL;
	}
	if (entry == NULL) {
		archive_read_free(outer);
		return 1;
	}

	/* use wrapper to read straight from this archive */
	inner = archive_read_new();
	archive_read_support_format_all(inner);
	archive_read_support_filter_all(inner);
	memset(&cb_ctx, 0, sizeof(cb_ctx));
	cb_ctx.archive = outer;
	if (archive_read_open(inner, &cb_ctx, NULL,
						  tree_gtree_read_cb,
						  tree_gtree_close_cb) != ARCHIVE_OK)
		warn("unable to read gtree data %s: %s", archive_entry_pathname(entry),
			 archive_error_string(inner));

	while (archive_read_next_header(inner, &entry) == ARCHIVE_OK) {
		const char *fname = archive_entry_pathname(entry);

		if (fname == NULL)
			continue;

		if (strncmp(fname, "caches/", sizeof("caches/") - 1) == 0) {
			char *nexttok = NULL;

			foundcaches = true;
			fname += sizeof("caches/") - 1;
			atom = atom_explode(fname);
			if (query != NULL && atom_compare(atom, query) != EQUAL)
				continue;

			/* ok, we're in business */
			len = archive_entry_size(entry);
			if (len > ctx->cache.storesize) {
				ctx->cache.storesize = len;
				ctx->cache.store     = xrealloc(ctx->cache.store, len);
			}
			archive_read_data(inner, ctx->cache.store, ctx->cache.storesize);

			memset(&meta, 0, sizeof(meta));
			/* entries are strictly single line, starting with KEY= (no
			 * whitespace) */
			for (p = strtok_r(ctx->cache.store, "=", &nexttok);
				 p != NULL;
				 p = strtok_r(NULL, "=", &nexttok))
			{
				char **ptr = NULL;

				if (1 == 0) {
					/* dummy case for syntax */
				}
#define match2(K,N) \
				else if (strcmp(p, #N) == 0) \
					ptr = &meta.Q_##K
#define match(K)  match2(K,K)
				match(DEPEND);
				match(RDEPEND);
				match(SLOT);
				match(SRC_URI);
				match(RESTRICT);
				match(LICENSE);
				match(DESCRIPTION);
				match(KEYWORDS);
				match(INHERITED);
				match(IUSE);
				match(CDEPEND);
				match(PDEPEND);
				match(PROVIDE);
				match(EAPI);
				match(PROPERTIES);
				match(BDEPEND);
				match(IDEPEND);
				match(DEFINED_PHASES);
				match(REQUIRED_USE);
				match(CONTENTS);
				match(USE);
				match(EPREFIX);
				match(PATH);
				match(BUILDID);
				match(SIZE);
				match2(_eclasses_, eclasses);
#undef match
#undef match2

				/* always advance to end of line, even when nothing
				 * matched */
				p = strtok_r(NULL, "\n", &nexttok);
				if (p == NULL)
					break;
				if (ptr != NULL)
					*ptr = p;
			}

			pkg.name    = atom->PF;
			pkg.slot    = meta.Q_SLOT == NULL ? (char *)"0" : meta.Q_SLOT;
			pkg.repo    = ctx->repo;
			pkg.atom    = atom;
			pkg.cat_ctx = cat;
			pkg.fd      = -2;  /* intentional, meta has already been read */

			if (strcmp(cat->name, atom->CATEGORY) != 0)
			{
				if (cat->pkg_ctxs != NULL) {
					atom_implode((depend_atom *)cat->pkg_ctxs);
					cat->pkg_ctxs = NULL;
				}
				cat->name     = atom->CATEGORY;
				cat->pkg_ctxs = (tree_pkg_ctx **)atom;  /* for name */
				atom          = NULL;  /* avoid double free */
			}

			/* do call callback with pkg_atom (populate cat and pkg) */
			ret |= callback(&pkg, priv);

			if (atom != NULL)
				atom_implode(atom);
		} else if (foundcaches) {
			break;  /* stop searching if we processed all cache entries */
		} else if (ctx->repo == NULL &&
				   strcmp(fname, "repository") == 0)
		{
			/* fill in repo, so it can be used when requested */
			len = archive_entry_size(entry);
			ctx->repo = xmalloc(len);
			archive_read_data(inner, ctx->repo, len);
		}
	}

	if (ctx->cache.storesize > 0)
		ctx->cache.store[0] = '\0';
	free(cat);

	return ret;
}
#endif

int
tree_foreach_pkg
(
	tree_ctx          *ctx,
	tree_pkg_cb        callback,
	void              *priv,
	bool               sort,
	const depend_atom *query
)
{
	tree_cat_ctx *cat_ctx;
	tree_pkg_ctx *pkg_ctx;
	int           ret;
	tree_pkg_cb  *origcb   = callback;
	void         *origpriv = priv;
	bool          postsort = false;
	bool          traverse = true;

	if (ctx == NULL)
		return EXIT_FAILURE;

	if (ctx->cache.all_categories) {
		/* always exploit the cache if it exists (next_cat doesn't
		 * consider it if sorting isn't requested) */
		sort = true;
	} else {
		/* perform sorting post retrieval by caching first:
		 * - binpkgs can be a combination of files and directories,
		 *   sorting must happen on the total result to be correct
		 * - packages is an external file that we need to read serially
		 *   and we can only sort on top of a cache
		 * - gtree should be fine, but since we can't assume it is
		 *   (others may start producing them), it's like packages
		 *   above, something we read serially not necessarily in the
		 *   correct order */
		if (sort &&
			(ctx->treetype == TREE_BINPKGS ||
			 ctx->treetype == TREE_PACKAGES ||
			 ctx->treetype == TREE_METADATA_GTREE))
		{
			callback = tree_foreach_cache_populate_cb;
			priv     = (void *)create_set();
			postsort = true;
		}

		/* handle Packages (binpkgs index) file separately */
		if (ctx->treetype == TREE_PACKAGES) {
			traverse = false;
			ret      = tree_foreach_packages(ctx, callback, priv);
		}

#ifdef ENABLE_GTREE
		/* similar a gtree cache can be read sequentially in one go */
		if (ctx->treetype == TREE_METADATA_GTREE) {
			traverse = false;
			ret      = tree_foreach_gtree(ctx, callback, priv);
		}
		if (ctx->treetype == TREE_EBUILD &&
			ctx->subtree != NULL &&
			ctx->subtree->treetype == TREE_METADATA_GTREE)
		{
			traverse = false;
			ret      = tree_foreach_gtree(ctx->subtree, callback, priv);
		}
#endif
	}

	ret = 0;
	if (traverse)
	{
		ctx->do_sort    = postsort ? false : sort;
		ctx->query_atom = query;

		while ((cat_ctx = tree_next_cat(ctx))) {
			if (callback != NULL) {
				while ((pkg_ctx = tree_next_pkg(cat_ctx))) {
					ret |= callback(pkg_ctx, priv);
					tree_close_pkg(pkg_ctx);
				}
			}
			tree_close_cat(cat_ctx);
		}

		/* allow foreach to be called again on the same open tree */
		if (ctx->do_sort &&
			ctx->cat_de != NULL)
		{
			scandir_free(ctx->cat_de, ctx->cat_cnt);
		} else if (ctx->dir != NULL) {
			rewinddir(ctx->dir);
		}
	}

	/* reset states */
	ctx->cat_de  = NULL;
	ctx->cat_cur = 0;
	ctx->cat_cnt = 0;
	ctx->dir     = NULL;
	ctx->do_sort = false;

	if (postsort) {
		DECLARE_ARRAY(cats);
		size_t n;

		/* should never happen, but perhaps a tree implementation
		 * populated something, then don't leak it */
		if (ctx->cache.categories != NULL)
			free_set(ctx->cache.categories);
		ctx->cache.categories     = priv;
		ctx->cache.all_categories = true;

		/* loop through all categories, and sort the pkgs */
		values_set(ctx->cache.categories, cats);
		array_for_each(cats, n, cat_ctx) {
			if (cat_ctx->pkg_cnt > 1) {
				qsort(cat_ctx->pkg_ctxs, cat_ctx->pkg_cnt,
					  sizeof(*cat_ctx->pkg_ctxs), tree_pkg_compar);
			}
		}
		xarrayfree_int(cats);

		/* do the final run this call was supposed to be for using the
		 * (sorted) cache, the callback can be empty for tree_match_atom
		 * when it wants to build a cache first */
		if (origcb != NULL)
			ret = tree_foreach_pkg_fast(ctx, origcb, origpriv, ctx->query_atom);
	}

	return ret;
}

depend_atom *
tree_get_atom(tree_pkg_ctx *pkg_ctx, bool complete)
{
	if (pkg_ctx->atom == NULL) {
		pkg_ctx->atom =
			atom_explode_cat(pkg_ctx->name, (char *)pkg_ctx->cat_ctx->name);
		if (pkg_ctx->atom == NULL)
			return NULL;
	}

	if (complete) {
		tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;
		if (ctx->treetype == TREE_VDB) {
			if (pkg_ctx->atom->SLOT == NULL) {
				/* FIXME: use tree_meta_get !!! */
				if (pkg_ctx->slot == NULL)
					tree_pkg_vdb_eat(pkg_ctx, "SLOT",
							&pkg_ctx->slot, &pkg_ctx->slot_len);
				pkg_ctx->atom->SLOT = pkg_ctx->slot;
			}
			if (pkg_ctx->atom->REPO == NULL) {
				if (pkg_ctx->repo == NULL)
					tree_pkg_vdb_eat(pkg_ctx, "repository",
							&pkg_ctx->repo, &pkg_ctx->repo_len);
				pkg_ctx->atom->REPO = pkg_ctx->repo;
			}
		} else { /* metadata, ebuild, binpkg or Packages */
			tree_pkg_meta *meta = NULL;
			if (pkg_ctx->atom->SLOT == NULL) {
				if (pkg_ctx->slot == NULL) {
					meta = tree_pkg_read(pkg_ctx);
					if (meta != NULL) {
						if (meta->Q_SLOT != NULL) {
							pkg_ctx->slot = xstrdup(meta->Q_SLOT);
							pkg_ctx->slot_len = strlen(pkg_ctx->slot);
						}
					}
				}
				pkg_ctx->atom->SLOT = pkg_ctx->slot;
			}
			/* repo is set from the tree, when found */
			if (pkg_ctx->atom->REPO == NULL) {
				if (pkg_ctx->repo == NULL && ctx->treetype == TREE_BINPKGS) {
					if (meta == NULL)
						meta = tree_pkg_read(pkg_ctx);
					if (meta != NULL && meta->Q_repository != NULL) {
						pkg_ctx->repo = xstrdup(meta->Q_repository);
						pkg_ctx->repo_len = strlen(pkg_ctx->repo);
					}
				}
				pkg_ctx->atom->REPO = pkg_ctx->repo;
			}
		}

		/* this is a bit atom territory, but since we pulled in SLOT we
		 * need to split it up in SLOT and SUBSLOT for atom_format to
		 * behave properly, this may be redundant but this probably
		 * isn't much of an issue */
		if (pkg_ctx->atom->SUBSLOT == NULL && pkg_ctx->atom->SLOT != NULL) {
			char *ptr;
			if ((ptr = strchr(pkg_ctx->atom->SLOT, '/')) != NULL) {
				*ptr++ = '\0';
			} else {
				/* PMS 7.2: When the sub-slot part is omitted from the
				 * SLOT definition, the package is considered to have an
				 * implicit sub-slot which is equal to the regular slot. */
				ptr = pkg_ctx->atom->SLOT;
			}
			pkg_ctx->atom->SUBSLOT = ptr;
		}
	}

	return pkg_ctx->atom;
}

struct get_atoms_state {
	set *cpf;
	bool fullcpv;
};

static int tree_get_atoms_cb(tree_pkg_ctx *pkg_ctx, void *priv)
{
	struct get_atoms_state *state = (struct get_atoms_state *)priv;
	depend_atom *atom = tree_get_atom(pkg_ctx, false);
	char abuf[BUFSIZ];

	if (state->fullcpv) {
		snprintf(abuf, sizeof(abuf), "%s/%s", atom->CATEGORY, atom->PF);
		state->cpf = add_set(abuf, state->cpf);
	} else {
		snprintf(abuf, sizeof(abuf), "%s/%s", atom->CATEGORY, atom->PN);
		state->cpf = add_set_unique(abuf, state->cpf, NULL);
	}

	return 0;
}

set *
tree_get_atoms(tree_ctx *ctx, bool fullcpv, set *satoms)
{
	struct get_atoms_state state = {
		.cpf = satoms,
		.fullcpv = fullcpv
	};

	tree_foreach_pkg_fast(ctx, tree_get_atoms_cb, &state, NULL);

	return state.cpf;
}

static tree_match_ctx *
tree_match_search_cat_int(
		tree_cat_ctx      *cat_ctx,
		const depend_atom *query,
		int                flags
)
{
	tree_pkg_ctx   *pkg_ctx;
	tree_match_ctx *ret      = NULL;
	depend_atom    *atom;
	char           *lastpn   = NULL;

	while ((pkg_ctx = tree_next_pkg(cat_ctx)) != NULL) {
		atom = tree_get_atom(pkg_ctx,
							 (query->SLOT != NULL ||
							  flags & TREE_MATCH_FULL_ATOM));
		/* skip virtual/ package as requested */
		if (!(flags & TREE_MATCH_VIRTUAL ||
			  strcmp(atom->CATEGORY, "virtual") != 0))
			continue;
		/* skip acct-* package as requested */
		if (!(flags & TREE_MATCH_ACCT ||
			  strncmp(atom->CATEGORY, "acct-", sizeof("acct-") - 1) != 0))
			continue;
		/* see if this atom matches the query */
		if (atom_compare(atom, query) == EQUAL) {
			tree_match_ctx *n;
			/* skip over additional versions for match latest */
			if (flags & TREE_MATCH_LATEST && lastpn != NULL &&
				strcmp(lastpn, atom->PN) == 0)
				continue;
			/* create a new match result */
			n = xzalloc(sizeof(tree_match_ctx));
			n->atom = atom;
			n->pkg = pkg_ctx;
			if (cat_ctx->ctx->treetype == TREE_PACKAGES &&
				pkg_ctx->meta->Q_PATH != NULL)
			{
				/* Packages file has a PATH ready for us */
				snprintf(n->path, sizeof(n->path), "%s/%s",
						 (char *)cat_ctx->ctx->path, pkg_ctx->meta->Q_PATH);
			} else if (pkg_ctx->binpkg_ismulti) {
				/* FEATURES=binpkg-multi-instance complicates things as
				 * it creates an intermediate PN directory like ebuilds,
				 * without multi-instance, it is more flat */
				snprintf(n->path, sizeof(n->path), "%s/%s/%s/%s%s",
						 (char *)cat_ctx->ctx->path,
						 atom->CATEGORY, atom->PN, pkg_ctx->name,
						 cat_ctx->ctx->treetype == TREE_EBUILD   ? ".ebuild" :
						 (cat_ctx->ctx->treetype == TREE_BINPKGS ||
						  cat_ctx->ctx->treetype == TREE_PACKAGES) ?
						 (pkg_ctx->binpkg_isgpkg   ? ".gpkg.tar" : ".xpak")  :
																   "");
			} else {
				snprintf(n->path, sizeof(n->path), "%s/%s/%s%s",
						 (char *)cat_ctx->ctx->path,
						 atom->CATEGORY, pkg_ctx->name,
						 cat_ctx->ctx->treetype == TREE_EBUILD   ? ".ebuild" :
						 (cat_ctx->ctx->treetype == TREE_BINPKGS ||
						  cat_ctx->ctx->treetype == TREE_PACKAGES) ?
						 (pkg_ctx->binpkg_isgpkg   ? ".gpkg.tar" : ".tbz2")  :
																   "");
			}
			if (flags & TREE_MATCH_METADATA)
				n->meta = tree_pkg_read(pkg_ctx);
			if (cat_ctx->ctx->treetype == TREE_BINPKGS ||
				cat_ctx->ctx->treetype == TREE_PACKAGES)
				n->free_atom = n->free_meta = 0;
			n->next = ret;
			ret = n;
			lastpn = atom->PN;
		}
		if (flags & TREE_MATCH_FIRST && ret != NULL)
			break;
	}
	cat_ctx->pkg_cur = 0;  /* reset to allow another traversal */

	return ret;
}

tree_match_ctx *
tree_match_atom(tree_ctx *ctx, const depend_atom *query, int flags)
{
	tree_cat_ctx *cat_ctx;
	tree_match_ctx *ret = NULL;

	ctx->query_atom = NULL;  /* if caching, ensure it contains ALL pkgs */

	/* activate cache for future lookups, tree_match_atom relies on
	 * cache behaviour from tree, which means all categories and
	 * packages remain in memory until tree_close is being called */
	if (ctx->cache.categories == NULL)
		tree_foreach_pkg_sorted(ctx, NULL, NULL, NULL);  /* force cache */

	ctx->do_sort = true;     /* often forces/enables cache usage */

	if (query->CATEGORY == NULL) {
		tree_match_ctx *tret;
		/* loop through all cats */
		while ((cat_ctx = tree_next_cat(ctx)) != NULL) {
			tret = tree_match_search_cat_int(cat_ctx, query, flags);
			if (tret != NULL)
				ret = tret;
			if (ret != NULL && flags & TREE_MATCH_FIRST)
				break;
		}
		/* allow running again through the cats */
		ctx->cat_cur = 0;
	} else {
		/* try CAT, and PN for latest version */
		if ((cat_ctx = tree_open_cat(ctx, query->CATEGORY)) != NULL)
			ret = tree_match_search_cat_int(cat_ctx, query, flags);
	}

	return ret;
}

void
tree_match_close(tree_match_ctx *match)
{
	tree_match_ctx *w;

	for (w = NULL; match != NULL; match = w) {
		w = match->next;
		if (match->free_atom)
			atom_implode(match->atom);
		if (match->free_meta && match->meta != NULL)
			tree_close_meta(match->meta);
		free(match);
	}
}
