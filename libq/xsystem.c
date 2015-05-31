/*
 * utility funcs
 *
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 */

#include <stdlib.h>
#include <sys/wait.h>

static void xsystem(const char *command)
{
	if (system(command))
		errp("system(%s) failed", command);
}

static void xsystembash(const char *command, int cwd)
{
	pid_t p = vfork();
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
		execl("/bin/bash", "bash", "--norc", "--noprofile", "-c", command, NULL);
		/* Hrm, still here ?  Maybe no bash ... */
		_exit(execl("/bin/sh", "sh", "-c", command, NULL));

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
