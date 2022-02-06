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

void xsystembash(const char *command, const char **argv, int cwd)
{
	pid_t p = fork();
	int status;

	switch (p) {
		case 0: /* child */
			if (cwd != AT_FDCWD) {
				if (fchdir(cwd)) {
					/* fchdir works with O_PATH starting w/linux-3.5 */
					if (errno == EBADF) {
						char *path;
						xasprintf(&path, "/proc/self/fd/%i", cwd);
						if (chdir(path))
							errp("chdir(%s) failed", path);
					} else {
						errp("fchdir(%i) failed", cwd);
					}
				}
			}
			if (argv == NULL) {
				execl(CONFIG_EPREFIX "bin/bash", "bash",
					  "--norc", "--noprofile", "-c", command, (char *)NULL);
				/* Hrm, still here ?  Maybe no bash ... */
				_exit(execl("/bin/sh", "sh", "-c", command, (char *)NULL));
			} else {
				int          argc = 0;
				const char  *a;
				const char **newargv;

				/* count existing args */
				for (a = argv[0]; a != NULL; a++, argc++)
					;
				argc += 1 + 1 + 1 + 1;
				newargv = xmalloc(sizeof(newargv[0]) * (argc + 1));
				argc = 0;
				newargv[argc++] = "bash";
				newargv[argc++] = "--norc";
				newargv[argc++] = "--noprofile";
				newargv[argc++] = "-c";
				for (a = argv[0]; a != NULL; a++)
					newargv[argc++] = a;
				newargv[argc] = NULL;

				execv(CONFIG_EPREFIX "bin/bash", (char *const *)newargv);

				/* Hrm, still here ?  Maybe no bash ... */
				newargv = &newargv[2];  /* shift, two args less */
				argc = 0;
				newargv[argc++] = "sh";
				_exit(execv("/bin/sh", (char *const *)newargv));
			}

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
