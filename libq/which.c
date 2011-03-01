/* find the path to a file by name */
static char *which(const char *fname)
{
	static char fullpath[BUFSIZ];
	char *ret, *path, *p;

	ret = NULL;

	path = getenv("PATH");
	if (!path)
		return ret;

	path = xstrdup(path);
	while ((p = strrchr(path, ':')) != NULL) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s", p + 1, fname);
		*p = 0;
		if (access(fullpath, R_OK) != -1) {
			ret = fullpath;
			break;
		}
	}
	free(path);

	return ret;
}
