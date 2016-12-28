/* Emulate `mkdir -p -m MODE PATH` */
static int mkdir_p_at(int dfd, const char *path, mode_t mode)
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
static int mkdir_p(const char *path, mode_t mode)
{
	return mkdir_p_at(AT_FDCWD, path, mode);
}

/* Emulate `rm -rf PATH` */
_q_static int rm_rf_at(int dfd, const char *path)
{
	int subdfd;
	DIR *dir;
	struct dirent *de;

	/* Cannot use O_PATH as we want to use fdopendir() */
	subdfd = openat(dfd, path, O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
	if (subdfd < 0)
		return -1;

	dir = fdopendir(subdfd);
	if (!dir) {
		close(subdfd);
		return -1;
	}

	while ((de = readdir(dir)) != NULL) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		if (unlinkat(subdfd, de->d_name, 0) == -1) {
			if (unlikely(errno != EISDIR))
				errp("could not unlink %s", de->d_name);
			rm_rf_at(subdfd, de->d_name);
			unlinkat(subdfd, de->d_name, AT_REMOVEDIR);
		}
	}

	/* this also does close(subdfd); */
	closedir(dir);

	return 0;
}

_q_static int rm_rf(const char *path)
{
	rm_rf_at(AT_FDCWD, path);

	if (rmdir(path) == 0)
		return 0;

	/* if path is a symlink, unlink it */
	if (unlink(path) == 0)
		return 0;

	/* XXX: we don't handle:
	 *      trailing slashes: `rm -rf a/b/c/` -> need to change to a/b/c */
	return -1;
}

_q_static int rmdir_r_at(int dfd, const char *path)
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

/*
_q_static int rmdir_r(const char *path)
{
	return rmdir_r_at(AT_FDCWD, path);
}
*/
