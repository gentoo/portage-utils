/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/quse.c,v 1.9 2005/06/14 00:16:05 vapier Exp $
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



#define QUSE_FLAGS "av" COMMON_FLAGS
static struct option const quse_long_opts[] = {
	{"all",        no_argument, NULL, 'a'},
	{"verbose",        no_argument, NULL, 'v'},
	COMMON_LONG_OPTS
};
static const char *quse_opts_help[] = {
	"List every package in the cache",
	"Show annoying things in IUSE",
	COMMON_OPTS_HELP
};
#define quse_usage(ret) usage(ret, QUSE_FLAGS, quse_long_opts, quse_opts_help, APPLET_QUSE)

static void print_highlighted_use_flags(char *str, int argc, char **argv) {
	char *p;
	size_t pos;

	short highlight = 0;
	int i;

	if (!color) {
		printf("%s", str);
		return;
	}

	remove_extra_space(str);
	rmspace(str);

	for (pos = 0; pos <= strlen(str); pos++) {
		if ( (p = strchr(str, ' ')) != NULL) {
			highlight = 0;
			*p = 0;
			pos += strlen(str)+1;
			for (i = 1; i < argc; i++) {
				if (strcmp(str, argv[i]) == 0)
					highlight = 1;
			}
			if (highlight)
				printf("%s%s%s ", BOLD, str, NORM);
			else
				printf("%s%s%s%s ", NORM, MAGENTA, str, NORM);
			str = p + 1;
		} else {
			highlight = 0;
			pos += strlen(str);
			for (i = 1; i < argc; i++) {
				if (strcmp(str, argv[i]) == 0)
					highlight = 1;
			}
			if (highlight)
				printf("%s%s%s", BOLD, str, NORM);
			else
				printf("%s%s%s%s", NORM, MAGENTA, str, NORM);
		}
	}
}

int quse_main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];
	int i, all = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QUSE, quse, "")) != -1) {
		switch (i) {
		case 'a': all = 1; break;
		case 'v': verbose = 1; break;
		COMMON_GETOPTS_CASES(quse)
		}
	}
	if (all) optind = argc;
	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
	fp = fopen(CACHE_EBUILD_FILE, "r");
	if (!fp)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			unsigned int lineno = 0;
			while ((fgets(buf, sizeof(buf), newfp)) != NULL) {
				int ok = 0;
				lineno++;
				if ((strncmp(buf, "IUSE=", 5)) != 0)
					continue;

				if ((p = strchr(buf, '\n')) != NULL)
					*p = 0;
				if (verbose) {
					if ((strchr(buf, '\t') != NULL)
					|| (strchr(buf, '\\') != NULL)
					|| (strchr(buf, '\'') != NULL)
					|| (strstr(buf, "  ") != NULL))
					warn("# Line %d of %s has an annoying %s", lineno, ebuild, buf);
				}
#if 0
				if ((p = strrchr(&buf[6], '\\')) != NULL) {
					size_t size = 0;
					char buf2[_POSIX_PATH_MAX];
					char buf3[_POSIX_PATH_MAX];

				multiline:
					*p = ' ';
					memset(buf2, 0, sizeof(buf2));

					fgets(buf2, sizeof(buf2), newfp);
					lineno++;

					if ((p = strrchr(buf2, '\n')) != NULL)
						*p = 0;
					snprintf(buf3, sizeof(buf3), "%s %s", buf, buf2);
					fprintf(stderr, "BUF='%s'\nBUF2='%s'\n", buf, buf2);
					remove_extra_space(buf3);
					fprintf(stderr, "BUF3='%s'\n", buf3);
					strcpy(buf, buf3);
					fprintf(stderr, "BUF0='%s'\nZBUF0[6]='%s'\n", buf, &buf[6]);
					if ((p = strrchr(buf2, '\\')) != NULL)
						goto multiline;
				}
#endif
				while ((p = strrchr(&buf[6], '"')) != NULL)
					*p = 0;
				while ((p = strrchr(&buf[6], '\'')) != NULL)
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
					printf("%s%s%s ", CYAN, ebuild, NORM);
					print_highlighted_use_flags(&buf[6], argc, argv);
					puts(NORM);
				}
				break;
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
