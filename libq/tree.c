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

#include "atom.h"
#include "eat_file.h"
#include "rmspace.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"

#include <ctype.h>
#include <xalloc.h>

static int tree_pkg_compar(const void *l, const void *r);

static tree_ctx *
tree_open_int(const char *sroot, const char *tdir, bool quiet)
{
	tree_ctx *ctx = xmalloc(sizeof(*ctx));

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

	ctx->dir = fdopendir(ctx->tree_fd);
	if (ctx->dir == NULL)
		goto cv_error;

	ctx->do_sort = false;
	ctx->cat_de = NULL;
	ctx->catsortfunc = alphasort;
	ctx->pkgsortfunc = tree_pkg_compar;
	ctx->repo = NULL;
	ctx->ebuilddir_ctx = NULL;
	ctx->ebuilddir_pkg_ctx = NULL;
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
	char *repo = NULL;
	size_t repolen = 0;

	snprintf(buf, sizeof(buf), "%s%s/%s", sroot, portdir, portrepo_name);
	if (eat_file(buf, &repo, &repolen)) {
		(void)rmspace(repo);
	} else {
		repo = NULL;  /* ignore missing repo file */
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_md5);
	ret = tree_open_int(sroot, buf, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_METADATA_MD5;
		ret->repo = repo;
		return ret;
	}

	snprintf(buf, sizeof(buf), "%s/%s", portdir, portcachedir_pms);
	ret = tree_open_int(sroot, buf, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_METADATA_PMS;
		ret->repo = repo;
		return ret;
	}

	ret = tree_open_int(sroot, portdir, true);
	if (ret != NULL) {
		ret->cachetype = CACHE_EBUILD;
		ret->repo = repo;
		return ret;
	}

	tree_close(ret);
	warnf("could not open repository at %s (under root %s)", portdir, sroot);

	return NULL;
}

tree_ctx *
tree_open_vdb(const char *sroot, const char *svdb)
{
	tree_ctx *ret = tree_open_int(sroot, svdb, false);
	if (ret != NULL)
		ret->cachetype = CACHE_VDB;
	return ret;
}

void
tree_close(tree_ctx *ctx)
{
	closedir(ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->tree_fd); */
	close(ctx->portroot_fd);
	if (ctx->do_sort)
		scandir_free(ctx->cat_de, ctx->cat_cnt);
	if (ctx->repo != NULL)
		free(ctx->repo);
	if (ctx->ebuilddir_ctx != NULL)
		free(ctx->ebuilddir_ctx);
	free(ctx);
}

int
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
	return cat_ctx;
}

tree_cat_ctx *
tree_next_cat(tree_ctx *ctx)
{
	/* search for a category directory */
	tree_cat_ctx *cat_ctx = NULL;

	if (ctx->do_sort) {
		if (ctx->cat_de == NULL) {
			ctx->cat_cnt = scandirat(ctx->tree_fd,
					".", &ctx->cat_de, tree_filter_cat, ctx->catsortfunc);
			ctx->cat_cur = 0;
		}

		while (ctx->cat_cur < ctx->cat_cnt) {
			cat_ctx = tree_open_cat(ctx, ctx->cat_de[ctx->cat_cur++]->d_name);
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

			if (tree_filter_cat(de) == 0)
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
	closedir(cat_ctx->dir);
	/* closedir() above does this for us: */
	/* close(ctx->fd); */
	if (cat_ctx->ctx->do_sort)
		free(cat_ctx->pkg_ctxs);
	free(cat_ctx);
}

int
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

	return i;
}

tree_pkg_ctx *
tree_open_pkg(tree_cat_ctx *cat_ctx, const char *name)
{
	tree_pkg_ctx *pkg_ctx = xmalloc(sizeof(*pkg_ctx));
	pkg_ctx->name = name;
	pkg_ctx->slot = NULL;
	pkg_ctx->repo = cat_ctx->ctx->repo;
	pkg_ctx->fd = -1;
	pkg_ctx->cat_ctx = cat_ctx;
	pkg_ctx->atom = NULL;
	return pkg_ctx;
}

static int
tree_pkg_compar(const void *l, const void *r)
{
	tree_pkg_ctx *pl = *(tree_pkg_ctx **)l;
	tree_pkg_ctx *pr = *(tree_pkg_ctx **)r;
	depend_atom *al = tree_get_atom(pl, false);
	depend_atom *ar = tree_get_atom(pr, false);

	switch (atom_compare(al, ar)) {
		case EQUAL:  return  0;
		case NEWER:  return -1;
		case OLDER:  return  1;
		default:     return strcmp(al->PN, ar->PN);
	}

	/* unreachable */
}

static tree_pkg_ctx *
tree_next_pkg_int(tree_cat_ctx *cat_ctx);
static tree_pkg_ctx *
tree_next_pkg_int(tree_cat_ctx *cat_ctx)
{
	tree_pkg_ctx *pkg_ctx = NULL;
	const struct dirent *de;

	if (cat_ctx->ctx->do_sort) {
		if (cat_ctx->pkg_ctxs == NULL) {
			size_t pkg_size = 0;
			cat_ctx->pkg_ctxs = NULL;
			cat_ctx->pkg_cnt = 0;
			cat_ctx->pkg_cur = 0;
			while ((de = readdir(cat_ctx->dir)) != NULL) {
				if (tree_filter_pkg(de) == 0)
					continue;

				if (cat_ctx->pkg_cnt == pkg_size) {
					pkg_size += 256;
					cat_ctx->pkg_ctxs = xrealloc(cat_ctx->pkg_ctxs,
								sizeof(*cat_ctx->pkg_ctxs) * pkg_size);
				}
				pkg_ctx = cat_ctx->pkg_ctxs[cat_ctx->pkg_cnt++] =
					tree_open_pkg(cat_ctx, de->d_name);
				if (pkg_ctx == NULL)
					cat_ctx->pkg_cnt--;
			}

			if (cat_ctx->ctx->pkgsortfunc != NULL) {
				qsort(cat_ctx->pkg_ctxs, cat_ctx->pkg_cnt,
						sizeof(*cat_ctx->pkg_ctxs), cat_ctx->ctx->pkgsortfunc);
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

	if (ctx->cachetype == CACHE_EBUILD) {
		char *p;

		/* serve *.ebuild files each as separate pkg_ctx with name set
		 * to CAT/P like in VDB and metadata */
		do {
			if (ctx->ebuilddir_pkg_ctx == NULL) {
				tree_ctx *pkgdir = ctx->ebuilddir_ctx;

				if (pkgdir == NULL)
					pkgdir = ctx->ebuilddir_ctx = xmalloc(sizeof(tree_ctx));
				memset(ctx->ebuilddir_ctx, '\0', sizeof(*ctx->ebuilddir_ctx));

				ctx->ebuilddir_pkg_ctx = tree_next_pkg_int(cat_ctx);
				if (ctx->ebuilddir_pkg_ctx == NULL)
					return NULL;

				pkgdir->portroot_fd = -1;
				pkgdir->tree_fd = cat_ctx->fd;
				pkgdir->do_sort = ctx->do_sort;
				pkgdir->catsortfunc = ctx->catsortfunc;
				pkgdir->pkgsortfunc = ctx->pkgsortfunc;
				pkgdir->repo = ctx->repo;
				pkgdir->cachetype = ctx->cachetype;

				ctx->ebuilddir_cat_ctx =
					tree_open_cat(pkgdir, ctx->ebuilddir_pkg_ctx->name);

				/* opening might fail if what we found wasn't a
				 * directory or something */
				if (ctx->ebuilddir_cat_ctx == NULL) {
					ctx->ebuilddir_pkg_ctx = NULL;
					return NULL;
				}

				/* "zap" the pkg such that it looks like CAT/P */
				ctx->ebuilddir_cat_ctx->name = cat_ctx->name;
			}

			ret = tree_next_pkg_int(ctx->ebuilddir_cat_ctx);
			if (ret == NULL) {
				tree_close_cat(ctx->ebuilddir_cat_ctx);
				ctx->ebuilddir_pkg_ctx = NULL;
			} else {
				if ((p = strstr(ret->name, ".ebuild")) == NULL) {
					tree_close_pkg(ret);
					ret = NULL;
				} else {
					*p = '\0';
				}
			}
		} while (ret == NULL);
	} else {
		ret = tree_next_pkg_int(cat_ctx);
	}

	return ret;
}

int
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

FILE *
tree_pkg_vdb_fopenat(
		tree_pkg_ctx *pkg_ctx,
		const char *file,
		int flags,
		mode_t mode,
		const char *fmode)
{
	FILE *fp;
	int fd;

	fd = tree_pkg_vdb_openat(pkg_ctx, file, flags, mode);
	if (fd == -1)
		return NULL;

	fp = fdopen(fd, fmode);
	if (!fp)
		close(fd);

	return fp;
}

bool
tree_pkg_vdb_eat(
		tree_pkg_ctx *pkg_ctx,
		const char *file,
		char **bufptr,
		size_t *buflen)
{
	int fd = tree_pkg_vdb_openat(pkg_ctx, file, O_RDONLY, 0);
	bool ret = eat_file_fd(fd, bufptr, buflen);
	rmspace(*bufptr);
	if (fd != -1)
		close(fd);
	return ret;
}

static tree_pkg_meta *
tree_read_file_pms(tree_pkg_ctx *pkg_ctx)
{
	struct stat s;
	char *ptr;
	FILE *f;
	tree_pkg_meta *ret = NULL;
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

tree_pkg_meta *
tree_pkg_read(tree_pkg_ctx *pkg_ctx)
{
	tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;

	if (pkg_ctx->fd == -1) {
		if (ctx->cachetype != CACHE_EBUILD) {
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
					O_RDONLY | O_CLOEXEC);
		} else {
			char *p = (char *)pkg_ctx->name;
			p += strlen(p);
			*p = '.';
			pkg_ctx->fd = openat(pkg_ctx->cat_ctx->fd, pkg_ctx->name,
					O_RDONLY | O_CLOEXEC);
			*p = '\0';
		}
		if (pkg_ctx->fd == -1)
			return NULL;
	}

	if (ctx->cachetype == CACHE_METADATA_MD5) {
		return tree_read_file_md5(pkg_ctx);
	} else if (ctx->cachetype == CACHE_METADATA_PMS) {
		return tree_read_file_pms(pkg_ctx);
	} else if (ctx->cachetype == CACHE_EBUILD) {
		return tree_read_file_ebuild(pkg_ctx);
	}

	warn("Unknown metadata cache type!");
	return NULL;
}

void
tree_close_meta(tree_pkg_meta *cache)
{
	if (!cache)
		errf("Cache is empty !");
	free(cache);
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

	if (ctx->cachetype == CACHE_EBUILD) {
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
	p = xbuf = xzalloc(len);
	if ((off_t)fread(p, 1, s.st_size, f) != s.st_size) {
		free(p);
		fclose(f);
		pkg_ctx->fd = -1;
		return NULL;
	}

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
	if (pkg_ctx->fd != -1)
		close(pkg_ctx->fd);
	if (pkg_ctx->atom != NULL)
		atom_implode(pkg_ctx->atom);
	/* avoid freeing tree_ctx' repo */
	if (pkg_ctx->cat_ctx->ctx->repo != pkg_ctx->repo)
		free(pkg_ctx->repo);
	free(pkg_ctx->slot);
	free(pkg_ctx);
}

int
tree_foreach_pkg(tree_ctx *ctx,
		tree_pkg_cb callback, void *priv, tree_cat_filter filter,
		bool sort, void *catsortfunc, void *pkgsortfunc)
{
	tree_cat_ctx *cat_ctx;
	tree_pkg_ctx *pkg_ctx;
	int ret;

	if (ctx == NULL)
		return EXIT_FAILURE;

	ctx->do_sort = sort;
	if (catsortfunc != NULL)
		ctx->catsortfunc = catsortfunc;
	if (pkgsortfunc != NULL)
		ctx->pkgsortfunc = pkgsortfunc;

	ret = 0;
	while ((cat_ctx = tree_next_cat(ctx))) {
		if (filter && !filter(cat_ctx, priv))
			continue;
		while ((pkg_ctx = tree_next_pkg(cat_ctx))) {
			ret |= callback(pkg_ctx, priv);
			tree_close_pkg(pkg_ctx);
		}
		tree_close_cat(cat_ctx);
	}

	return ret;
}

depend_atom *
tree_get_atom(tree_pkg_ctx *pkg_ctx, bool complete)
{
	if (pkg_ctx->atom == NULL) {
		pkg_ctx->atom = atom_explode(pkg_ctx->name);
		if (pkg_ctx->atom == NULL)
			return NULL;
		pkg_ctx->atom->CATEGORY = (char *)pkg_ctx->cat_ctx->name;
	}

	if (complete) {
		tree_ctx *ctx = pkg_ctx->cat_ctx->ctx;
		if (ctx->cachetype == CACHE_VDB) {
			if (pkg_ctx->atom->SLOT == NULL) {
				tree_pkg_vdb_eat(pkg_ctx, "SLOT",
						&pkg_ctx->slot, &pkg_ctx->slot_len);
				pkg_ctx->atom->SLOT = pkg_ctx->slot;
			}
			if (pkg_ctx->atom->REPO == NULL) {
				tree_pkg_vdb_eat(pkg_ctx, "repository",
						&pkg_ctx->repo, &pkg_ctx->repo_len);
				pkg_ctx->atom->REPO = pkg_ctx->repo;
			}
		} else { /* metadata or ebuild */
			if (pkg_ctx->atom->SLOT == NULL) {
				tree_pkg_meta *meta = tree_pkg_read(pkg_ctx);
				if (meta != NULL) {
					pkg_ctx->slot = xstrdup(meta->SLOT);
					pkg_ctx->slot_len = strlen(pkg_ctx->slot);
					pkg_ctx->atom->SLOT = pkg_ctx->slot;
					tree_close_meta(meta);
				}
			}
			/* repo is set from the tree, when found */
			if (pkg_ctx->atom->REPO == NULL)
				pkg_ctx->atom->REPO = pkg_ctx->repo;
		}
	}

	return pkg_ctx->atom;
}

set *
tree_get_vdb_atoms(const char *sroot, const char *svdb, int fullcpv)
{
	tree_ctx *ctx;

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

	ctx = tree_open_vdb(sroot, svdb);
	if (!ctx)
		return NULL;

	/* scan the cat first */
	cfd = scandirat(ctx->tree_fd, ".", &cat, tree_filter_cat, alphasort);
	if (cfd < 0)
		goto fuckit;

	for (j = 0; j < cfd; j++) {
		dfd = scandirat(ctx->tree_fd, cat[j]->d_name,
				&pf, tree_filter_pkg, alphasort);
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
			eat_file_at(ctx->tree_fd, buf, &slotp, &slot_len);
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
	tree_close(ctx);
	return cpf;
}
