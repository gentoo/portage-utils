/*
 * Copyright 2005-2011 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/scandirat.c,v 1.2 2011/12/18 06:31:29 vapier Exp $
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2011 Mike Frysinger  - <vapier@gentoo.org>
 */

static int scandirat(int dir_fd, const char *dir, struct dirent ***dirlist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	int fd, cnt;
	DIR *dirp;
	struct dirent *de, **ret;

	fd = openat(dir_fd, dir, O_RDONLY|O_CLOEXEC);
	if (fd == -1)
		return -1;
	dirp = fdopendir(fd);
	if (!dirp) {
		close(fd);
		return -1;
	}

	ret = NULL;
	cnt = 0;
	while ((de = readdir(dirp))) {
		if (filter(de) == 0)
			continue;

		ret = realloc(ret, sizeof(*ret) * (cnt + 1));
		ret[cnt++] = xmemdup(de, sizeof(*de));
	}
	*dirlist = ret;

	qsort(ret, cnt, sizeof(*ret), (void *)compar);

	/* closes underlying fd */
	closedir(dirp);

	return cnt;
}

_q_static void scandir_free(struct dirent **de, int cnt)
{
	if (cnt <= 0)
		return;

	while (cnt--)
		free(de[cnt]);
	free(de);
}
