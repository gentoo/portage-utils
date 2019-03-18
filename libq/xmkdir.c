/*
 * Copyright 2011-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2011-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "xmalloc.h"
#include "xmkdir.h"

/* Emulate `mkdir -p -m MODE PATH` */
int
mkdir_p_at(int dfd, const char *path, mode_t mode)
{
	char *_p, *p, *s;

	/* Assume that most of the time, only the last element
	 * is missing.  So if we can mkdir it right away, bail. */
	if (mkdirat(dfd, path, mode) == 0 || errno == EEXIST)
		return 0;

	/* Build up the whole tree */
	_p = p = xstrdup(path);

	while (*p) {
		/* Skip duplicate slashes */
		while (*p == '/')
			++p;

		/* Find the next path element */
		s = strchr(p, '/');
		if (!s) {
			mkdirat(dfd, _p, mode);
			break;
		}

		/* Make it */
		*s = '\0';
		mkdirat(dfd, _p, mode);
		*s = '/';

		p = s;
	}

	free(_p);

	return 0;
}

int
mkdir_p(const char *path, mode_t mode)
{
	return mkdir_p_at(AT_FDCWD, path, mode);
}

/* Emulate `rm -rf PATH` */
int
rm_rf_at(int dfd, const char *path)
{
	int subdfd;
	DIR *dir;
	struct dirent *de;
	int ret = 0;

	/* Cannot use O_PATH as we want to use fdopendir() */
	subdfd = openat(dfd, path, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
	if (subdfd < 0)
		return -1;

	dir = fdopendir(subdfd);
	if (!dir) {
		close(subdfd);
		return unlinkat(dfd, path, 0);
	}

	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (unlinkat(subdfd, de->d_name, 0) == -1) {
			if (unlikely(errno != EISDIR)) {
				struct stat st;
				/* above is a linux short-cut, we really just want to
				 * know whether we're really with a directory or not */
				if (fstatat(subdfd, de->d_name, &st, 0) != 0 ||
						!(st.st_mode & S_IFDIR))
					errp("could not unlink %s", de->d_name);
			}
			ret |= rm_rf_at(subdfd, de->d_name);
		}
	}

	ret |= unlinkat(dfd, path, AT_REMOVEDIR);

	/* this also does close(subdfd); */
	closedir(dir);

	return ret;
}

int
rm_rf(const char *path)
{
	return rm_rf_at(AT_FDCWD, path);
}

int
rmdir_r_at(int dfd, const char *path)
{
	size_t len;
	char *p, *e;

	p = xstrdup_len(path, &len);
	e = p + len;

	while (e != p) {
		if (unlinkat(dfd, p, AT_REMOVEDIR) && errno == ENOTEMPTY)
			break;
		while (*e != '/' && e > p)
			--e;
		*e = '\0';
	}

	free(p);

	return 0;
}

int
rmdir_r(const char *path)
{
	return rmdir_r_at(AT_FDCWD, path);
}
