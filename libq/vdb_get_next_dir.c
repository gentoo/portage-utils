static struct dirent *
q_vdb_get_next_dir(DIR *dir)
{
	/* search for a category directory */
	struct dirent *ret;

next_entry:
	ret = readdir(dir);
	if (ret == NULL) {
		closedir(dir);
		return NULL;
	}

	if (q_vdb_filter_cat(ret) == 0)
		goto next_entry;

	return ret;
}
