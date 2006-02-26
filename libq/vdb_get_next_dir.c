#include <sys/types.h>
#include <dirent.h>

struct dirent *q_vdb_get_next_dir(DIR *dir);
struct dirent *q_vdb_get_next_dir(DIR *dir)
{
	/* search for a category directory */
	struct dirent *ret;

next_entry:
	ret = readdir(dir);
	if (ret == NULL) {
		closedir(dir);
		return NULL;
	}

	if (ret->d_name[0] == '.' || ret->d_name[0] == '-')
		goto next_entry;
	if (strchr(ret->d_name, '-') == NULL)
		if ((strcmp(ret->d_name, "virtual")) != 0)
			goto next_entry;

	return ret;
}

