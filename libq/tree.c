/*
 * Copyright 2005-2025 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2008 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdbool.h>
#include <stddef.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <xalloc.h>
#include "md5.h"
#include "sha1.h"

#include "atom.h"
#include "eat_file.h"
#include "hash.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xpak.h"

#include <ctype.h>
#include <xalloc.h>

static int tree_pkg_compar(const void *l, const void *r);
static tree_pkg_ctx *tree_next_pkg_int(tree_cat_ctx *cat_ctx);
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
	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_md5);
	ret->subtree = tree_open_int(sroot, buf, true);
	if (ret->subtree != NULL) {
		ret->subtree->treetype = TREE_METADATA_MD5;
		return ret;
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_pms);
	ret->subtree = tree_open_int(sroot, buf, true);
	if (ret->subtree != NULL) {
		ret->subtree->treetype = TREE_METADATA_PMS;
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
			/* ensure we cleanup all pkgs */
			cat->pkg_cur = 0;
			tree_close_cat(cat);
		}

		xarrayfree_int(t);
	}
	if (ctx->cache.store != NULL)
		free(ctx->cache.store);

	closedir(ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->tree_fd); */
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

	cat_ctx = xmalloc(sizeof(*cat_ctx));
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
				ctx->cat_cnt = scandirat(ctx->tree_fd,
						".", &ctx->cat_de, tree_filter_cat, alphasort);
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
			contains_set(cat_ctx->name, cat_ctx->ctx->cache.categories))
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

#ifdef DT_UNKNOWN
	/* pkg must be a file */
	if (de->d_type != DT_REG)
		return 0;
#endif

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

	if (cat_ctx->ctx->treetype == TREE_EBUILD &&
		cat_ctx->ctx->ebuilddir_cat_ctx == cat_ctx)
	{
		char *p;
		if ((p = strstr(name, ".ebuild")) == NULL)
			return NULL;  /* invalid, must be some random other file */
		*p = '\0';
	} else if (cat_ctx->ctx->treetype == TREE_BINPKGS) {
		char *p;
		if ((p = strstr(name, ".tbz2")) == NULL)
			return NULL;  /* invalid, no support for .gpkg yet */
		*p = '\0';
	}

	pkg_ctx = xzalloc(sizeof(*pkg_ctx));
	pkg_ctx->name = name;
	pkg_ctx->repo = cat_ctx->ctx->repo;
	pkg_ctx->fd = -1;
	pkg_ctx->cat_ctx = cat_ctx;

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
					/* name was freed by tree_close_pkg on fail */
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
	tree_ctx *ctx = cat_ctx->ctx;
	tree_pkg_ctx *ret = NULL;

	if (ctx->treetype == TREE_EBUILD) {
		/* serve *.ebuild files each as separate pkg_ctx with name set
		 * to CAT/P like in VDB and metadata */
		do {
			if (ctx->ebuilddir_pkg_ctx == NULL) {
				tree_ctx *pkgdir = ctx->ebuilddir_ctx;

				if (pkgdir == NULL)
					pkgdir = ctx->ebuilddir_ctx = xzalloc(sizeof(tree_ctx));

				ctx->ebuilddir_pkg_ctx = tree_next_pkg_int(cat_ctx);
				if (ctx->ebuilddir_pkg_ctx == NULL)
					return NULL;

				pkgdir->portroot_fd = -1;
				pkgdir->tree_fd = cat_ctx->fd;
				pkgdir->do_sort = ctx->do_sort;
				pkgdir->repo = ctx->repo;
				pkgdir->treetype = ctx->treetype;
				pkgdir->subtree = ctx->subtree;

				ctx->ebuilddir_cat_ctx =
					tree_open_cat(pkgdir, ctx->ebuilddir_pkg_ctx->name);

				/* opening might fail if what we found wasn't a
				 * directory or something */
				if (ctx->ebuilddir_cat_ctx == NULL) {
					ctx->ebuilddir_pkg_ctx = NULL;
					continue;
				}

				/* "zap" the pkg such that it looks like CAT/P */
				ctx->ebuilddir_cat_ctx->name = cat_ctx->name;
				ctx->ebuilddir_cat_ctx->ctx = ctx;
			}

			ret = tree_next_pkg_int(ctx->ebuilddir_cat_ctx);
			if (ret == NULL) {
				tree_close_cat(ctx->ebuilddir_cat_ctx);
				ctx->ebuilddir_pkg_ctx = NULL;
			}
		} while (ret == NULL);
	} else {
		ret = tree_next_pkg_int(cat_ctx);
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
							/* implement line continuation (\ before newline) */
							if (esc && (*p == '\n' || *p == '\r'))
								*p = ' ';
							esc = false;
						}

						*w++ = *p++;
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
	int newfd = dup(pkg_ctx->fd);

	xpak_process_fd(pkg_ctx->fd, true, m, tree_read_file_binpkg_xpak_cb);
	pkg_ctx->fd = -1;  /* closed by xpak_process_fd */

	/* fill in some properties which are not available, but would be in
	 * Packages, and used to verify the package ... this is somewhat
	 * fake, but allows to transparantly use a dir of binpkgs */
	if (newfd != -1) {
		size_t fsize;
		size_t needlen = SHA1_DIGEST_SIZE + 1 + 19 + 1;
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
		m->Q_SIZE = m->Q_SHA1 + SHA1_DIGEST_SIZE + 1;
		m->storage->pos += needlen;

		lseek(newfd, 0, SEEK_SET);  /* reposition at the whole file */
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
					 ctx->treetype == TREE_EBUILD ? "ebuild" : "tbz2");
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
					char    srcmd5[MD5_DIGEST_SIZE];
					size_t  flen;

					if (hash_multiple_file_fd(pkg_ctx->fd,
										  	  srcmd5, NULL, NULL, NULL,
										  	  NULL, &flen, HASH_MD5) == 0)
						pkg_ctx->fd = -1;

					mdmd5 = tree_pkg_meta_get(spkg, _md5_);
					/* TODO: eclass compares */

					/* is this a valid cache? use it! */
					if (mdmd5 != NULL &&
						memcmp(mdmd5, srcmd5, MD5_DIGEST_SIZE) == 0)
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
						close(spkg->fd);
						spkg->fd = -1;
					} else {
						ret = tree_pkg_read(spkg);
						close(pkg_ctx->fd);
						pkg_ctx->fd = -1;
					}
				}
			}
		}
		if (ret == NULL) {
			(void)tree_pkg_read_openfd_int(pkg_ctx);  /* re-open if fallback */
			ret = tree_read_file_ebuild(pkg_ctx);
		}
	} else if (ctx->treetype == TREE_BINPKGS) {
		ret = tree_read_file_binpkg(pkg_ctx);
	} else if (ctx->treetype == TREE_PACKAGES) {
		// The meta should have been in pkg_ctx->meta
		warn("TREE_PACHAGES pkg_ctx->meta is NULL");
		ret = NULL;
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

// The next are methods & types used internally by tree_foreach_packages
typedef struct {
	tree_pkg_ctx *pkgs_queue;
	size_t pkgs_queue_len;
	size_t pkgs_queue_cap;

	tree_cat_ctx *cat_ctx;
	// Kept alongside cat_cache->name in order to be able to free
	char *cat_ctx_name;

	// Input values
	tree_ctx *ctx;
        tree_pkg_cb *callback;
	void *callback_data;
} _tree_foreach_packages_state;

static int
_tree_foreach_packages_compare(const void *l, const void *r)
{
	const tree_pkg_ctx *a = l, *b = r;
	switch (atom_compare(a->atom, b->atom)) {
	case NEWER:
		return -1;
	case OLDER:
		return 1;
	default:
		return 0;
	}
}

static void
_tree_foreach_packages_destroy_package(tree_pkg_ctx *p)
{
	if (p->atom) atom_implode(p->atom);
	if (p->meta) free(p->meta);
	memset(p, 0, sizeof(*p));
}

static tree_cat_ctx *
_tree_foreach_packages_get_cat_ctx(_tree_foreach_packages_state *state,
                                   const char *cat)
{
	// If the cached category doesn't exist or is a mismatch recreate it
        if (state->cat_ctx == NULL || strcmp(cat, state->cat_ctx->name) != 0) {
		if (state->cat_ctx != NULL)
			tree_close_cat(state->cat_ctx);
                if (state->cat_ctx_name != NULL)
			free(state->cat_ctx_name);

		// We use this after `cat` has been free'd by atom_implode
		state->cat_ctx_name = xstrdup(cat);
		state->cat_ctx = tree_open_cat(state->ctx, state->cat_ctx_name);

                if (state->cat_ctx == NULL) {
			free(state->cat_ctx_name);
			state->cat_ctx_name = NULL;
			// probably dir doesn't exist or something,
			// generate a dummy cat
			state->cat_ctx = tree_open_cat(state->ctx, ".");
		}
	}

	return state->cat_ctx;
}

static int
_tree_foreach_packages_do_callback(_tree_foreach_packages_state *state,
                                   tree_pkg_ctx *p)
{
	const char *cat = p->atom->CATEGORY;
	p->cat_ctx = _tree_foreach_packages_get_cat_ctx(state, cat);
	int result = state->callback(p, state->callback_data);

	_tree_foreach_packages_destroy_package(p);

	return result;
}

// Check if the top-most package in state.queue has the same CAT and PN as p
// If there aren't package in the queue this function returns true
static bool
_tree_foreach_packages_can_enqueue(_tree_foreach_packages_state *state,
				   tree_pkg_ctx *p) {
	if (state->pkgs_queue_len == 0) return true;

        depend_atom *a = p->atom;
	depend_atom *b = state->pkgs_queue[state->pkgs_queue_len - 1].atom;

        if (a->CATEGORY == NULL || b->CATEGORY == NULL || strcmp(a->CATEGORY, b->CATEGORY) != 0)
		return false;
        if (a->PN == NULL || b->PN == NULL || strcmp(a->PN, b->PN) != 0)
		return false;
	return true;
}

static int
_tree_foreach_packages_dequeue(_tree_foreach_packages_state *state)
{
	size_t i;
	const char *cat;
	int ret = 0;

	if (state->pkgs_queue_len == 0) return ret;

	if (state->pkgs_queue_len > 1)
		qsort(state->pkgs_queue, state->pkgs_queue_len, sizeof(state->pkgs_queue[0]),
		      _tree_foreach_packages_compare);

        for (i = 0; i < state->pkgs_queue_len; ++i)
		ret |= _tree_foreach_packages_do_callback(state, &state->pkgs_queue[i]);

	state->pkgs_queue_len = 0;
	return ret;
}

// When this returns true then:
//   1) result->meta is VALID and, after use, will need to be free'd
//   2) result->atom is VALID and, after use, will need to be atom_implode'd
// If this function returns false then:
//   1) result contains garbage data
static bool
_tree_foreach_packages_get_package(char **data, size_t *size, tree_pkg_ctx *result)
{
	// Used for the default SLOT, has to be of type char*
	static char ZERO[] = "0";

	memset(result, 0, sizeof(*result));
	result->meta = xzalloc(sizeof(*result->meta));

        do {
		char *q, *c;

		/* find next line */
		c = NULL;
		for (q = *data; *size > 0 && *q != '\n'; q++, -- *size)
			if (c == NULL && *q == ':')
				c = q;

		if (*size == 0) break;

		bool end_of_block = *data == q;
		const char *line_start = *data;
		*data = q + 1; /* hop over \n */
		-- *size;

                if (end_of_block) {
			// If we didn't find a CPV line then clear the data and keep going
                        if (result->atom == NULL) {
				tree_pkg_meta *meta = result->meta;
				memset(meta, 0, sizeof(*meta));
				memset(result, 0, sizeof(*result));
				result->meta = meta;

				continue;
			}

			// Fill the package's data and return success
			if (result->meta->Q_BUILDID)
				result->atom->BUILDID = atoi(result->meta->Q_BUILDID);
			result->slot = result->meta->Q_SLOT ? result->meta->Q_SLOT : ZERO;
			result->name = result->atom->PF;
			result->repo = result->meta->Q_repository;
			result->fd = -2; /* intentional, meta has already been read */

			return true;
		}

		/* skip invalid lines */
		if (c == NULL || q - c < 3 || c[1] != ' ')
			continue;

		const char *key = line_start;
		char *value = c + 2; /* hop over ": " */
		/* NULL-terminate key and value */
		*q = '\0';
		*c = '\0';

		if (strcmp(key, "CPV") == 0) {
			if (result->atom != NULL) atom_implode(result->atom);
			result->atom = atom_explode(value);

#               define match_key(X) match_key2(X,X)
#               define match_key2(X,Y) \
			} else if (strcmp(key, #X) == 0) {		\
				result->meta->Q_##Y = value;

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

#               undef match_key2
#               undef match_key

		}
	} while (*size > 0);

	_tree_foreach_packages_destroy_package(result);
	return false; // Reached EOF, no package has been read
}

static void
_tree_foreach_packages_enqueue(_tree_foreach_packages_state *state, tree_pkg_ctx *p)
{
	// Create space for the next package
	if (state->pkgs_queue_len == state->pkgs_queue_cap) {
		state->pkgs_queue_cap += 64;
		state->pkgs_queue = xreallocarray(state->pkgs_queue, state->pkgs_queue_cap, sizeof(state->pkgs_queue[0]));
	}

	state->pkgs_queue[state->pkgs_queue_len ++] = *p;
}

static int
tree_foreach_packages(tree_ctx *ctx, tree_pkg_cb callback, void *priv)
{
	int ret = 0;
	const depend_atom *query = ctx->query_atom;
	size_t i;

	// The Packages file, read into memory
        char *data;
	size_t size;

	// The Packages file is sorted ascending by version. To stay
	// consistent with the rest of the outputs we sort it
	// descending. The package appear in CAT/PN order so it's
	// only needed to sort per-pkg
	bool use_queue = ctx->do_sort;

	_tree_foreach_packages_state state;

	state.ctx = ctx;
	state.callback = callback;
	state.callback_data = priv;
	state.cat_ctx = NULL;
	state.cat_ctx_name = NULL;

	if (use_queue) {
		state.pkgs_queue_cap = 64;
		state.pkgs_queue = xcalloc(state.pkgs_queue_cap, sizeof(state.pkgs_queue[0]));
		state.pkgs_queue_len = 0;
	}


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

	data = ctx->cache.store;
	size = strlen(ctx->cache.store);  /* sucks, need eat_file change */

        while (true) {
		tree_pkg_ctx p;
		bool found = _tree_foreach_packages_get_package(&data, &size, &p);
		if (!found) break;

                if (query != NULL && atom_compare(query, p.atom) != EQUAL) {
			_tree_foreach_packages_destroy_package(&p);
			continue;
		}

                if (!use_queue) {
			_tree_foreach_packages_do_callback(&state, &p);
                        continue;
		}

		// Not the same CAT/PN? => clear the queue
		if (!_tree_foreach_packages_can_enqueue(&state, &p))
			ret |= _tree_foreach_packages_dequeue(&state);

		_tree_foreach_packages_enqueue(&state, &p);
	}

	// Clear out all pending packages, if they exist
	ret |= _tree_foreach_packages_dequeue(&state);

        if (use_queue)
		free(state.pkgs_queue);
	if (state.cat_ctx)
		tree_close_cat(state.cat_ctx);
	if (state.cat_ctx_name)
		free(state.cat_ctx_name);

	/* ensure we don't free a garbage pointer */
	ctx->cache.store[0] = '\0';

	return ret;
}

int
tree_foreach_pkg(tree_ctx *ctx, tree_pkg_cb callback, void *priv,
		bool sort, const depend_atom *query)
{
	tree_cat_ctx *cat_ctx;
	tree_pkg_ctx *pkg_ctx;
	int ret;

	if (ctx == NULL)
		return EXIT_FAILURE;

	ctx->do_sort = sort;
	ctx->query_atom = query;

	/* handle Packages (binpkgs index) file separately */
	if (ctx->treetype == TREE_PACKAGES)
		return tree_foreach_packages(ctx, callback, priv);

	ret = 0;
	while ((cat_ctx = tree_next_cat(ctx))) {
		while ((pkg_ctx = tree_next_pkg(cat_ctx))) {
			ret |= callback(pkg_ctx, priv);
			tree_close_pkg(pkg_ctx);
		}
		tree_close_cat(cat_ctx);
	}

	/* allow foreach to be called again on the same open tree */
	if (ctx->do_sort)
		scandir_free(ctx->cat_de, ctx->cat_cnt);
	else
		rewinddir(ctx->dir);
	ctx->cat_de = NULL;
	ctx->cat_cur = 0;
	ctx->cat_cnt = 0;
	ctx->do_sort = false;

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

static int
tree_match_atom_cache_populate_cb(tree_pkg_ctx *ctx, void *priv)
{
	tree_cat_ctx  *cat_ctx;
	tree_pkg_ctx  *pkg;
	set           *cache   = priv;
	tree_ctx      *tctx    = ctx->cat_ctx->ctx;
	depend_atom   *atom    = tree_get_atom(ctx, true);
	tree_pkg_meta *meta    = tree_pkg_read(ctx);

	(void)priv;

	cat_ctx = get_set(atom->CATEGORY, cache);
	if (cat_ctx == NULL) {
		cat_ctx = tree_open_cat(tctx, ".");
		if (cache != NULL)  /* for static code analysers */
			add_set_value(atom->CATEGORY, cat_ctx, NULL, cache);
		/* get a pointer from the set */
		cat_ctx->name = contains_set(atom->CATEGORY, cache);
	}

	pkg = xcalloc(1, sizeof(*pkg));

	/* FIXME: this really could use a set */
	cat_ctx->pkg_cnt++;
	cat_ctx->pkg_ctxs = xrealloc(cat_ctx->pkg_ctxs,
			sizeof(*cat_ctx->pkg_ctxs) * cat_ctx->pkg_cnt);
	cat_ctx->pkg_ctxs[cat_ctx->pkg_cnt - 1] = pkg;
	pkg->cat_ctx = cat_ctx;
	pkg->atom = atom_clone(atom);
	pkg->name = xstrdup(pkg->atom->PF);
	pkg->repo = tctx->repo != NULL ? xstrdup(tctx->repo) : NULL;
	if (meta != NULL) {
		pkg->fd = -2;  /* don't try to read, we fill it in here */
		if (tctx->treetype == TREE_PACKAGES) {
			/* need to copy, source is based on temp space in foreach */
			pkg->meta = tree_clone_meta(meta);
		} else {
			/* BINPKG case, this one is read/allocated separately from
			 * xpak archive, so can just take it over */
			pkg->meta = meta;
			ctx->meta = NULL;  /* avoid double free */
		}
	} else {
		pkg->meta = NULL;
	}

	return 0;
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
				/* binpkg-multi-instance has a PATH ready for us */
				snprintf(n->path, sizeof(n->path), "%s/%s",
						 (char *)cat_ctx->ctx->path, pkg_ctx->meta->Q_PATH);
			} else {
				snprintf(n->path, sizeof(n->path), "%s/%s/%s%s",
						 (char *)cat_ctx->ctx->path,
						 cat_ctx->name, pkg_ctx->name,
						 cat_ctx->ctx->treetype == TREE_EBUILD   ? ".ebuild" :
						 cat_ctx->ctx->treetype == TREE_BINPKGS  ? ".tbz2"   :
						 cat_ctx->ctx->treetype == TREE_PACKAGES ? ".tbz2"   :
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

	ctx->do_sort = true;     /* sort uses buffer, which cache relies on */
	ctx->query_atom = NULL;  /* ensure the cache contains ALL pkgs */

	if ((ctx->treetype == TREE_PACKAGES ||
		 ctx->treetype == TREE_BINPKGS) &&
		ctx->cache.categories == NULL)
	{
		set *cache;
		DECLARE_ARRAY(cats);
		size_t n;

		/* Reading these formats requires us to traverse the tree for
		 * each operation, so we better cache the entire thing in one
		 * go.  Packages in addition may not store the atoms sorted
		 * (it's a file with content after all) and since we rely on
		 * this for latest and first match, it is important that we sort
		 * the contents so we're sure */

		cache = ctx->cache.categories;
		if (ctx->cache.categories != NULL)
			ctx->cache.categories = NULL;
		else
			cache = create_set();

		tree_foreach_pkg(ctx,
				tree_match_atom_cache_populate_cb, cache, true, NULL);

		ctx->do_sort = true;  /* turn it back on after tree_foreach_pkg */
		ctx->cache.all_categories = true;
		ctx->cache.categories = cache;

		/* loop through all categories, and sort the pkgs */
		values_set(cache, cats);
		array_for_each(cats, n, cat_ctx) {
			if (cat_ctx->pkg_cnt > 1) {
				qsort(cat_ctx->pkg_ctxs, cat_ctx->pkg_cnt,
						sizeof(*cat_ctx->pkg_ctxs), tree_pkg_compar);
			}
		}
		xarrayfree_int(cats);
	}

	/* activate cache for future lookups, tree_match_atom relies on
	 * cache behaviour from tree, which means all categories and
	 * packages remain in memory until tree_close is being called */
	if (ctx->cache.categories == NULL)
		ctx->cache.categories = create_set();

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
