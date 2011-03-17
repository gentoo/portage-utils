#include <sys/types.h>
#include <dirent.h>

struct dirent *q_vdb_get_next_dir(DIR *dir);
struct dirent *q_vdb_get_next_dir(DIR *dir)
{
	/* search for a category directory */
	struct dirent *ret;
	int len, i;

next_entry:
	ret = readdir(dir);
	if (ret == NULL) {
		closedir(dir);
		return NULL;
	}

	if (ret->d_name[0] == '.' || ret->d_name[0] == '-')
		goto next_entry;

	len = strlen(ret->d_name);
	for (i = 0; i < len; i++) {
		if (!isalnum(ret->d_name[i])) { /* [A-Za-z0-9+_.-] */
			switch (ret->d_name[i]) {
				case '+':
				case '_':
				case '.':
				case '-':
					break;
				default:
					goto next_entry;
			}
		}
	}
	return ret;
}
