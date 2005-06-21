/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/quse.c,v 1.14 2005/06/21 16:07:20 solar Exp $
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



#define QUSE_FLAGS "avKL" COMMON_FLAGS
static struct option const quse_long_opts[] = {
	{"all",       no_argument, NULL, 'a'},
	{"verbose",   no_argument, NULL, 'v'},
	{"keywords",  no_argument, NULL, 'K'},
	{"licence",   no_argument, NULL, 'L'},
	/* {"format",     a_argument, NULL, 'F'}, */
	COMMON_LONG_OPTS
};
static const char *quse_opts_help[] = {
	"List every package in the cache",
	"Show annoying things in IUSE",
	"Use the KEYWORDS vs IUSE",
	"Use the LICENSE vs IUSE",
	/* "Use you own variable formats. -F NAME=", */
	COMMON_OPTS_HELP
};
#define quse_usage(ret) usage(ret, QUSE_FLAGS, quse_long_opts, quse_opts_help, APPLET_QUSE)

static void print_highlighted_use_flags(char *str, int ind, int argc, char **argv) {
	char *p;
	size_t pos, len;

	short highlight = 0;
	int i;

	if (!color) {
		printf("%s", str);
		return;
	}

	remove_extra_space(str);
	rmspace(str);

	len = strlen(str);

	for (pos = 0; pos < len; pos++) {
		highlight = 0;
		if ((p = strchr(str, ' ')) != NULL)
			*p = 0;
		pos += strlen(str);
		for (i = ind; i < argc; ++i)
			if (strcmp(str, argv[i]) == 0)
				highlight = 1;
		if (highlight)
			printf("%s%s%s ", BOLD, str, NORM);
		else
			printf("%s%s%s%s ", NORM, MAGENTA, str, NORM);
		if (p != NULL)
			str = p + 1;
	}
}

int quse_main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	char buf[2][_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];
	const char *search_var = NULL;
	const char *search_vars[] = { "IUSE=", "KEYWORDS=", "LICENSE=", search_var };
	short all=0;
	int i, idx=0;
	int search_len;

	all = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QUSE, quse, "")) != -1) {
		switch (i) {
		case 'a': all = 1; break;
		case 'v': verbose = 1; break;
		case 'K': idx = 1;  break;
		case 'L': idx = 2; break;
		/* case 'F': idx = 3, search_vars[idx] = xstrdup(optarg); break; */
		COMMON_GETOPTS_CASES(quse)
		}
	}
	if (all) optind = argc;
	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */

	search_len = strlen(search_vars[idx]);

	if ((fp = fopen(CACHE_EBUILD_FILE, "r")) == NULL)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			unsigned int lineno = 0;
			while ((fgets(buf[0], sizeof(buf[0]), newfp)) != NULL) {
				int ok = 0;
				lineno++;

				if ((strncmp(buf[0], search_vars[idx], search_len)) != 0)
					continue;

				if ((p = strchr(buf[0], '\n')) != NULL)
					*p = 0;
				if (verbose) {
					if ((strchr(buf[0], '\t') != NULL)
					|| (strchr(buf[0], '\\') != NULL)
					|| (strchr(buf[0], '\'') != NULL)
					|| (strstr(buf[0], "  ") != NULL))
					warn("# Line %d of %s has an annoying %s", lineno, ebuild, buf[0]);
				}
				if ((p = strrchr(&buf[0][search_len+1], '\\')) != NULL) {

				multiline:
					*p = ' ';
					memset(buf[1], 0, sizeof(buf[1]));

					fgets(buf[1], sizeof(buf[1]), newfp);
					lineno++;

					if ((p = strchr(buf[1], '\n')) != NULL)
						*p = 0;
					snprintf(buf[2], sizeof(buf[2]), "%s %s", buf[0], buf[1]);
					remove_extra_space(buf[2]);
					strcpy(buf[0], buf[2]);
					if ((p = strrchr(buf[1], '\\')) != NULL)
						goto multiline;
				}

				while ((p = strrchr(&buf[0][search_len+1], '"')) != NULL)  *p = 0;
				while ((p = strrchr(&buf[0][search_len+1], '\'')) != NULL) *p = 0;
				while ((p = strrchr(&buf[0][search_len+1], '\\')) != NULL) *p = ' ';

				if ((size_t)strlen(buf[0]) < (size_t)(search_len+1)) {
					warnf("err '%s'/%d <= %d\n", buf[0], strlen(buf[0]), search_len+1);
					continue;
				}

				if ((argc == optind) || (all)) {
					ok = 1;
				} else {
					ok = 0;
					for (i = optind; i < argc; ++i) {
						if (rematch(argv[i], &buf[0][search_len+1], REG_NOSUB) == 0) {
							ok = 1;
							break;
						}
					}
				}
				if (ok) {
					printf("%s%s%s ", CYAN, ebuild, NORM);
					print_highlighted_use_flags(&buf[0][search_len+1], optind, argc, argv);
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
	return EXIT_SUCCESS;
}
