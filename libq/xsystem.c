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

static void xsystembash(const char *command)
{
	pid_t p = vfork();
	int status;

	switch (p) {
	case 0: /* child */
		execl("/bin/bash", "bash", "--norc", "--noprofile", "-c", command, NULL);
		/* Hrm, still here ?  Maybe no bash ... */
		_exit(execl("/bin/sh", "sh", "-c", command, NULL));

	default: /* parent */
		waitpid(p, &status, 0);
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) == 0)
				return;
			else
				exit(WEXITSTATUS(status));
		}
		/* fall through */

	case -1: /* fucked */
		errp("xsystembash(%s) failed", command);
	}
}
