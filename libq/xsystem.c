/*
 * Copyright 2010-2022 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2010-2016 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2022-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"

#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "xasprintf.h"
#include "xsystem.h"

void xsystem(const char *command)
{
	if (unlikely(system(command)))
		errp("system(%s) failed", command);
}

void xsystembash(const char *command, int cwd)
{
	pid_t p = fork();
	int status;

	switch (p) {
	case 0: /* child */
		if (cwd != AT_FDCWD)
			if (fchdir(cwd)) {
				/* fchdir works with O_PATH starting w/linux-3.5 */
				if (errno == EBADF) {
					char *path;
					xasprintf(&path, "/proc/self/fd/%i", cwd);
					if (chdir(path))
						errp("chdir(%s) failed", path);
				} else
					errp("fchdir(%i) failed", cwd);
			}
		execl(CONFIG_EPREFIX "bin/bash", "bash",
				"--norc", "--noprofile", "-c", command, (char *)NULL);
		/* Hrm, still here ?  Maybe no bash ... */
		_exit(execl("/bin/sh", "sh", "-c", command, (char *)NULL));

	default: /* parent */
		waitpid(p, &status, 0);
		if (WIFSIGNALED(status)) {
			err("phase crashed with signal %i: %s", WTERMSIG(status),
			    strsignal(WTERMSIG(status)));
		} else if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				return;
			else
				err("phase exited %i", WEXITSTATUS(status));
		}
		/* fall through */

	case -1: /* fucked */
		errp("xsystembash(%s) failed", command);
	}
}
