/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.15 2005/06/20 04:37:29 vapier Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */



#define Q_FLAGS "ir" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",      no_argument, NULL, 'i'},
	{"reinitialize", no_argument, NULL, 'r'},
	COMMON_LONG_OPTS
};
static const char *q_opts_help[] = {
	"Install symlinks for applets",
	"Reinitialize ebuild cache",
	COMMON_OPTS_HELP
};
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, APPLET_Q)


int q_main(int argc, char **argv)
{
	int i;
	char *p;
	APPLET func;

	if (argc == 0)
		return 1;

	argv0 = p = basename(argv[0]);

	if ((func = lookup_applet(p)) == 0)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	while ((i = GETOPT_LONG(Q, q, "+")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(q)
		case 'r': reinitialize = 1; return 0;
		case 'i': {
			char buf[_POSIX_PATH_MAX];
			printf("Installing symlinks:\n");
			memset(buf, 0x00, sizeof(buf));
			if ((readlink("/proc/self/exe", buf, sizeof(buf))) == (-1)) {
				warnf("could not readlink '/proc/self/exe': %s", strerror(errno));
				return 1;
			}
			if (chdir(dirname(buf)) != 0) {
				warnf("could not chdir to '%s': %s", buf, strerror(errno));
				return 1;
			}
			for (i = 1; i <= LAST_APPLET; ++i) {
				printf(" %s ...", applets[i].name);
				errno = 0;
				symlink("q", applets[i].name);
				printf("\t[%s]\n", strerror(errno));
			}
			return 0;
		}
		}
	}
	if (argc == optind)
		q_usage(EXIT_FAILURE);
	if ((func = lookup_applet(argv[optind])) == 0)
		return 1;

	optind = 0; /* reset so the applets can call getopt */

	return (func)(argc - 1, ++argv);
}
