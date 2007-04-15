/* find the path to a file by name */
static char *which(const char *fname)
{
	static char fullpath[BUFSIZ];
	char *path, *p;
	path = getenv("PATH");
	if (!path)
		return NULL;
	path = xstrdup(path);
	while ((p = strrchr(path, ':')) != NULL) {
		snprintf(fullpath, sizeof(fullpath), "%s/%s", p + 1, fname);
		*p = 0;
		if (access(fullpath, R_OK) != (-1)) {
			free(path);
			return (char *) fullpath;
		}
	}
	free(path);
	return NULL;
}
