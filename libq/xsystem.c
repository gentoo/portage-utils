/*
 * utility funcs
 *
 * Copyright 2005-2010 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/libq/xsystem.c,v 1.2 2011/02/21 22:02:59 vapier Exp $
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
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			return;
		/* fall through */

	case -1: /* fucked */
		errp("xsystembash(%s) failed", command);
	}
}
