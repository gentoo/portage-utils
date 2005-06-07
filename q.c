/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/q.c,v 1.11 2005/06/07 04:37:32 vapier Exp $
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



#define Q_FLAGS "i" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"install",   no_argument, NULL, 'i'},
	COMMON_LONG_OPTS
};
static const char *q_opts_help[] = {
	"Install symlinks for applets",
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

	p = argv0 = basename(argv[0]);

	if ((func = lookup_applet(p)) == 0)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	while ((i=getopt_long(argc, argv, "+" Q_FLAGS, q_long_opts, NULL)) != -1) {
		switch (i) {

		case 'V': version_barf(); break;
		case 'h': q_usage(EXIT_SUCCESS); break;

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
			for (i = 1; applets[i].name; ++i) {
				printf(" %s ...", applets[i].name);
				errno = 0;
				symlink("q", applets[i].name);
				printf("\t[%s]\n", strerror(errno));
			}
			return 0;
		}

		default: break;
		}
	}

	if ((func = lookup_applet(argv[1])) == 0)
		return 1;

	return (func)(argc - 1, ++argv);
}
