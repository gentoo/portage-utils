/*
 * Copyright 2005-2019 Gentoo Foundation
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

#include "cache.h"
#include "eat_file.h"
#include "rmspace.h"
#include "scandirat.h"
#include "vdb.h"

#ifdef EBUG
static void
cache_dump(cache_pkg_meta *cache)
{
	if (!cache)
		errf("Cache is empty !");

	printf("DEPEND     : %s\n", cache->DEPEND);
	printf("RDEPEND    : %s\n", cache->RDEPEND);
	printf("SLOT       : %s\n", cache->SLOT);
	printf("SRC_URI    : %s\n", cache->SRC_URI);
	printf("RESTRICT   : %s\n", cache->RESTRICT);
	printf("HOMEPAGE   : %s\n", cache->HOMEPAGE);
	printf("LICENSE    : %s\n", cache->LICENSE);
	printf("DESCRIPTION: %s\n", cache->DESCRIPTION);
	printf("KEYWORDS   : %s\n", cache->KEYWORDS);
	printf("INHERITED  : %s\n", cache->INHERITED);
	printf("IUSE       : %s\n", cache->IUSE);
	printf("CDEPEND    : %s\n", cache->CDEPEND);
	printf("PDEPEND    : %s\n", cache->PDEPEND);
	printf("PROVIDE    : %s\n", cache->PROVIDE);
	printf("EAPI       : %s\n", cache->EAPI);
	printf("PROPERTIES : %s\n", cache->PROPERTIES);
}
#endif

static const char portcachedir_pms[] = "metadata/cache";
static const char portcachedir_md5[] = "metadata/md5-cache";
static const char portrepo_name[]    = "profiles/repo_name";
cache_ctx *
cache_open(const char *sroot, const char *portdir)
{
	cache_ctx *ret;
	char buf[_Q_PATH_MAX];
	char *repo = NULL;
	size_t repolen = 0;

	snprintf(buf, sizeof(buf), "%s%s/%s", sroot, portdir, portrepo_name);
	if (eat_file(buf, &repo, &repolen)) {
		(void)rmspace(repo);
	} else {
		repo = NULL;  /* ignore missing repo file */
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_md5);
	ret = q_vdb_open2(sroot, buf, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_METADATA_MD5;
		ret->repo = repo;
		return ret;
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_pms);
	ret = q_vdb_open2(sroot, buf, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_METADATA_PMS;
		ret->repo = repo;
		return ret;
	}

	ret = q_vdb_open2(sroot, portdir, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_EBUILD;
		ret->repo = repo;
		return ret;
	}

	cache_close(ret);
	warnf("could not open repository at %s (under root %s)", portdir, sroot);

	return NULL;
}

void
cache_close(cache_ctx *ctx)
{
	if (ctx->repo != NULL)
		free(ctx->repo);
	if (ctx->ebuilddir_ctx != NULL)
		free(ctx->ebuilddir_ctx);
	q_vdb_close(ctx);
}

cache_cat_ctx *
cache_open_cat(cache_ctx *ctx, const char *name)
{
	return q_vdb_open_cat(ctx, name);
}

cache_cat_ctx *
cache_next_cat(cache_ctx *ctx)
{
	return q_vdb_next_cat(ctx);
}

void
cache_close_cat(cache_cat_ctx *cat_ctx)
{
	return q_vdb_close_cat(cat_ctx);
}

cache_pkg_ctx *
cache_open_pkg(cache_cat_ctx *cat_ctx, const char *name)
{
	return q_vdb_open_pkg(cat_ctx, name);
}

cache_pkg_ctx *
cache_next_pkg(cache_cat_ctx *cat_ctx)
{
	cache_ctx *ctx = (cache_ctx *)cat_ctx->ctx;
	cache_pkg_ctx *ret = NULL;

	if (ctx->cachetype == CACHE_EBUILD) {
		char *p;

		/* serve *.ebuild files each as separate pkg_ctx with name set
		 * to CAT/P like in VDB and metadata */
		do {
			if (ctx->ebuilddir_pkg_ctx == NULL) {
				q_vdb_ctx *pkgdir = ctx->ebuilddir_ctx;

				if (pkgdir == NULL)
					pkgdir = ctx->ebuilddir_ctx = xmalloc(sizeof(q_vdb_ctx));
				memset(ctx->ebuilddir_ctx, '\0', sizeof(*ctx->ebuilddir_ctx));

				if ((ctx->ebuilddir_pkg_ctx = q_vdb_next_pkg(cat_ctx)) == NULL)
					return NULL;

				pkgdir->portroot_fd = -1;
				pkgdir->vdb_fd = cat_ctx->fd;
				pkgdir->do_sort = ctx->do_sort;
				pkgdir->repo = ctx->repo;
				pkgdir->cachetype = ctx->cachetype;

				ctx->ebuilddir_cat_ctx =
					q_vdb_open_cat(pkgdir, ctx->ebuilddir_pkg_ctx->name);
			}

			ret = q_vdb_next_pkg(ctx->ebuilddir_cat_ctx);
			if (ret == NULL) {
				q_vdb_close_cat(ctx->ebuilddir_cat_ctx);
				ctx->ebuilddir_pkg_ctx = NULL;
			} else {
				if ((p = strstr(ret->name, ".ebuild")) == NULL) {
					cache_close_pkg(ret);
					ret = NULL;
				} else {
					/* "zap" the pkg such that it looks like CAT/P */
					ret->cat_ctx->name = cat_ctx->name;
					*p = '\0';
				}
			}
		} while (ret == NULL);
	} else {
		ret = q_vdb_next_pkg(cat_ctx);
	}

	return ret;
}

static cache_pkg_meta *
cache_read_file_pms(cache_pkg_ctx *pkg_ctx)
{
	struct stat s;
	char *ptr;
	FILE *f;
	cache_pkg_meta *ret = NULL;
	size_t len;
	char buf[_Q_PATH_MAX];

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	ret->DEPEND = ret->_data;
#define next_line(curr, next) \
	if ((ptr = strchr(ret->curr, '\n')) == NULL) { \
		warn("Invalid cache file for '%s'", buf); \
		goto err; \
	} \
	ret->next = ptr+1; \
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
		warn("Invalid cache file for '%s' - could not find end of cache data",
				buf);
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
		cache_close_meta(ret);
	return NULL;
}

static cache_pkg_meta *
cache_read_file_md5(cache_pkg_ctx *pkg_ctx)
{
	struct stat s;
	char *ptr, *endptr;
	FILE *f;
	cache_pkg_meta *ret = NULL;
	size_t len;

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	ptr = (char*)ret;
	ret->_data = ptr + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	/* We have a block of key=value\n data.
	 * KEY=VALUE\n
	 * Where KEY does NOT contain:
	 * \0 \n =
	 * And VALUE does NOT contain:
	 * \0 \n
	 * */
#define assign_var_cmp(keyname, cmpkey) \
	if (strncmp(keyptr, cmpkey, strlen(cmpkey)) == 0) { \
		ret->keyname = valptr; \
		continue; \
	}
#define assign_var(keyname) \
	assign_var_cmp(keyname, #keyname);

	ptr = ret->_data;
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
		assign_var(_eclasses_);
		assign_var(_md5_);
		warn("Cache file for '%s/%s' has unknown key %s",
				pkg_ctx->cat_ctx->name, pkg_ctx->name, keyptr);
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
		cache_close_meta(ret);
	return NULL;
}

static cache_pkg_meta *
cache_read_file_ebuild(cache_pkg_ctx *pkg_ctx)
{
	FILE *f;
	struct stat s;
	cache_pkg_meta *ret = NULL;
	size_t len;
	char *p;
	char *q;
	char *r;
	char **key;
	bool findnl;

	if ((f = fdopen(pkg_ctx->fd, "r")) == NULL)
		goto err;

	if (fstat(pkg_ctx->fd, &s) != 0)
		goto err;

	len = sizeof(*ret) + s.st_size + 1;
	ret = xzalloc(len);
	p = (char *)ret;
	ret->_data = p + sizeof(*ret);
	if ((off_t)fread(ret->_data, 1, s.st_size, f) != s.st_size)
		goto err;

	p = ret->_data;
	do {
		q = p;
		while (*p >= 'A' && *p <= 'Z')
			p++;

		key = NULL;
		if (q < p && *p == '=') {
			*p++ = '\0';
			/* match variable against which ones we look for */
#define match_key(X) else if (strcmp(q, #X) == 0) key = &ret->X
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
#undef match_key
		}

		findnl = true;
		if (key != NULL) {
			q = p;
			if (*q == '"' || *q == '\'') {
				/* find matching quote */
				q = p;
				do {
					while (*p != '\0' && *p != *q)
						p++;
					if (*p == *q) {
						for (r = p - 1; r > q; r--)
							if (*r != '\\')
								break;
						if (r != q && (p - 1 - r) % 2 == 1)
							continue;
					}
					break;
				} while (1);
				q++;
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
		cache_close_meta(ret);
	return NULL;
}

cache_pkg_meta *
cache_pkg_read(cache_pkg_ctx *pkg_ctx)
{
	cache_ctx *ctx = (cache_ctx *)(pkg_ctx->cat_ctx->ctx);

	if (pkg_ctx->fd == -1) {
		if (ctx->cachetype != CACHE_EBUILD) {
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
					O_RDONLY|O_CLOEXEC);
		} else {
			char *p = (char *)pkg_ctx->name;
			p += strlen(p);
			*p = '.';
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
					O_RDONLY|O_CLOEXEC);
			*p = '\0';
		}
		if (pkg_ctx->fd == -1)
			return NULL;
	}

	if (ctx->cachetype == CACHE_METADATA_MD5) {
		return cache_read_file_md5(pkg_ctx);
	} else if (ctx->cachetype == CACHE_METADATA_PMS) {
		return cache_read_file_pms(pkg_ctx);
	} else if (ctx->cachetype == CACHE_EBUILD) {
		return cache_read_file_ebuild(pkg_ctx);
	}

	warn("Unknown metadata cache type!");
	return NULL;
}

void
cache_close_meta(cache_pkg_meta *cache)
{
	if (!cache)
		errf("Cache is empty !");
	free(cache);
}

void
cache_close_pkg(cache_pkg_ctx *pkg_ctx)
{
	/* avoid free of cache_ctx' repo by q_vdb_close_pkg */
	if (pkg_ctx->cat_ctx->ctx->repo == pkg_ctx->repo)
		pkg_ctx->repo = NULL;

	q_vdb_close_pkg(pkg_ctx);
}

static int
cache_foreach_pkg_int(const char *sroot, const char *portdir,
		q_vdb_pkg_cb callback, void *priv, q_vdb_cat_filter filter,
		bool sort, void *catsortfunc, void *pkgsortfunc)
{
	cache_ctx *ctx;
	cache_cat_ctx *cat_ctx;
	cache_pkg_ctx *pkg_ctx;
	int ret;

	ctx = cache_open(sroot, portdir);
	if (!ctx)
		return EXIT_FAILURE;

	ctx->do_sort = sort;
	if (catsortfunc != NULL)
		ctx->catsortfunc = catsortfunc;
	if (pkgsortfunc != NULL)
		ctx->pkgsortfunc = pkgsortfunc;

	ret = 0;
	while ((cat_ctx = cache_next_cat(ctx))) {
		if (filter && !filter(cat_ctx, priv))
			continue;
		while ((pkg_ctx = cache_next_pkg(cat_ctx))) {
			ret |= callback(pkg_ctx, priv);
			cache_close_pkg(pkg_ctx);
		}
		cache_close_cat(cat_ctx);
	}
	cache_close(ctx);

	return ret;
}

int
cache_foreach_pkg(const char *sroot, const char *portdir,
		q_vdb_pkg_cb callback, void *priv, q_vdb_cat_filter filter)
{
	return cache_foreach_pkg_int(sroot, portdir, callback, priv,
			filter, false, NULL, NULL);
}

int
cache_foreach_pkg_sorted(const char *sroot, const char *portdir,
		q_vdb_pkg_cb callback, void *priv,
		void *catsortfunc, void *pkgsortfunc)
{
	return cache_foreach_pkg_int(sroot, portdir, callback, priv,
			NULL, true, catsortfunc, pkgsortfunc);
}
