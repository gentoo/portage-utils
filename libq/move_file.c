/*
 * Copyright 2005-2021 Gentoo Authors
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "stat-time.h"

#include "copy_file.h"
#include "move_file.h"

int
move_file(int rootfd_src, const char *name_src,
		  int rootfd_dst, const char *name_dst,
		  struct stat *stat_src)
{
	/* first try fast path -- src/dst are same device, else
	 * fall back to slow path -- manual read/write */
	if (renameat(rootfd_src, name_src, rootfd_dst, name_dst) != 0) {
		int             fd_src;
		int             fd_dst;
		char            tmpname_dst[_Q_PATH_MAX];
		struct stat     st;
		struct timespec times[2];

		fd_src = openat(rootfd_src, name_src, O_RDONLY|O_CLOEXEC);
		if (fd_src < 0) {
			warnp("could not read source file %s", name_src);
			return fd_src;
		}

		if (stat_src == NULL) {
			if (fstat(fd_src, &st) != 0) {
				warnp("could not stat source file %s", name_src);
				return -1;
			}

			stat_src = &st;
		}

		/* do not write the file in place ...
	 	 * will fail with files that are in use
	 	 * plus it isn't atomic, so we could leave a mess */
	 	snprintf(tmpname_dst, sizeof(tmpname_dst), ".%u.%s",
	 			 getpid(), name_dst);
		fd_dst = openat(rootfd_dst, tmpname_dst,
					 	O_WRONLY|O_CLOEXEC|O_CREAT|O_TRUNC,
					 	stat_src->st_mode);
		if (fd_dst < 0) {
			warnp("could not open destination file %s (for %s)",
				  tmpname_dst, name_dst);
			close(fd_src);
			return fd_dst;
		}

		/* make sure owner/mode is sane before we write out data */
		if (fchown(fd_dst, stat_src->st_uid, stat_src->st_gid) != 0) {
			warnp("could not set ownership (%zu/%zu) for %s",
			  	  (size_t)stat_src->st_uid, (size_t)stat_src->st_gid, name_dst);
			return -1;
		}
		if (fchmod(fd_dst, stat_src->st_mode) != 0) {
			warnp("could not set permission (%u) for %s",
			  	  (int)stat_src->st_mode, name_dst);
			return -1;
		}

		/* do the actual data copy */
		if (copy_file_fd(fd_src, fd_dst)) {
			warnp("could not write to file %s", name_dst);
			if (unlinkat(rootfd_dst, tmpname_dst, 0) != 0) {
				/* don't care */;
			}
			close(fd_src);
			close(fd_dst);
			return -1;
		}

		/* Preserve the file times */
		times[0] = get_stat_atime(&st);
		times[1] = get_stat_mtime(&st);
		futimens(fd_dst, times);

		close(fd_src);
		close(fd_dst);

		/* finally move the new tmp dst file to the right place, which
		 * should be on the same FS/device now */
		if (renameat(rootfd_dst, tmpname_dst, rootfd_dst, name_dst)) {
			warnp("could not rename %s to %s", tmpname_dst, name_dst);
			return -1;
		}
	}

	return 0;
}
