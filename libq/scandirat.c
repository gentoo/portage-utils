/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#if !defined(HAVE_SCANDIRAT)
# if defined(__GLIBC__) && (__GLIBC__ << 8 | __GLIBC_MINOR__) > (2 << 8 | 14)
#  define HAVE_SCANDIRAT
# endif
#endif

#if !defined(HAVE_SCANDIRAT)

static int
scandirat(int dir_fd, const char *dir, struct dirent ***dirlist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	int fd;
	DIR *dirp;
	struct dirent *de, **ret;
	size_t retlen = 0;
	size_t retsize = 0;
#define INCRSZ 64

	/* Cannot use O_PATH as we want to use fdopendir() */
	fd = openat(dir_fd, dir, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return -1;
	dirp = fdopendir(fd);
	if (!dirp) {
		close(fd);
		return -1;
	}

	ret = NULL;
	while ((de = readdir(dirp))) {
		size_t sdesz;
		size_t sdenamelen;

		if (filter(de) == 0)
			continue;

		if (retlen == retsize) {
			retsize += INCRSZ;
			ret = xrealloc(ret, sizeof(*ret) * retsize);
		}
		sdesz = (void *)de->d_name - (void *)de;
		sdenamelen = strlen(de->d_name) + 1;
		ret[retlen] = xmalloc(sdesz + sdenamelen);
		memcpy(ret[retlen], de, sdesz);
		strncpy(ret[retlen]->d_name, de->d_name, sdenamelen);
		retlen++;
	}
	*dirlist = ret;

	qsort(ret, retlen, sizeof(*ret), (void *)compar);

	/* closes underlying fd */
	closedir(dirp);

	return (int)retlen;
}

#endif

static void
scandir_free(struct dirent **de, int cnt)
{
	if (cnt <= 0)
		return;

	while (cnt--)
		free(de[cnt]);
	free(de);
}
