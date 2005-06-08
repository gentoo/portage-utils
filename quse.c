/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/quse.c,v 1.2 2005/06/08 23:34:05 vapier Exp $
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



#define QUSE_FLAGS "" COMMON_FLAGS
static struct option const quse_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *quse_opts_help[] = {
	COMMON_OPTS_HELP
};
#define quse_usage(ret) usage(ret, QUSE_FLAGS, quse_long_opts, quse_opts_help, APPLET_QUSE)



int quse_main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];
	int i;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QUSE, quse, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(quse)
		}
	}

	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
	fp = fopen(EBUILD_CACHE, "r");
	if (!fp)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			while ((fgets(buf, sizeof(buf), newfp)) != NULL) {
				if ((strncmp(buf, "IUSE=", 5)) == 0) {
					int ok;
					if ((p = strrchr(&buf[6], '"')) != NULL)
						*p = 0;
					if (argc == optind) {
						ok = 1;
					} else {
						ok = 0;
						for (i = optind; i < argc; ++i) {
							if (rematch(argv[i], &buf[6], REG_NOSUB) == 0) {
								ok = 1;
								break;
							}
						}
					}
					if (ok) {
						if (color)
							printf(BLUE "%s" NORM " %s\n", ebuild, &buf[6]);
						else
							printf("%s %s\n", ebuild, &buf[6]);
					}
					break;
				}
			}
			fclose(newfp);
		} else {
			if (!reinitialize)
				warnf("(cache update pending) %s : %s", ebuild, strerror(errno));
			reinitialize = 1;
		}
	}
	fclose(fp);
	return 0;
}
