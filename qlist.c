/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.3 2005/06/08 23:33:40 vapier Exp $
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



#define QLIST_FLAGS "" COMMON_FLAGS
static struct option const qlist_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qlist_opts_help[] = {
	COMMON_OPTS_HELP
};
#define qlist_usage(ret) usage(ret, QLIST_FLAGS, qlist_long_opts, qlist_opts_help, APPLET_QLIST)



int qlist_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	struct dirent *dentry, *de;
	char *cat, *p, *q;
	const char *path = "/var/db/pkg";
	struct stat st;
	size_t len = 0;
	char buf[_POSIX_PATH_MAX];
	char buf2[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		}
	}

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return 1;

	p = q = cat = NULL;

	if (argc > 1) {
		cat = strchr(argv[optind], '/');
		len = strlen(argv[optind]);
	}
	while ((dentry = readdir(dir))) {
		if (*dentry->d_name == '.')
			continue;
		if ((strchr((char *) dentry->d_name, '-')) == 0)
			continue;
		stat(dentry->d_name, &st);
		if (!(S_ISDIR(st.st_mode)))
			continue;
		chdir(dentry->d_name);
		if ((dirp = opendir(".")) == NULL)
			continue;
		while ((de = readdir(dirp))) {
			if (*de->d_name == '.')
				continue;
			if (argc > 1) {
				if (cat != NULL) {
					snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name,
					         de->d_name);
					/*if ((rematch(argv[optind], buf, REG_EXTENDED)) != 0)*/
					if ((strncmp(argv[optind], buf, len)) != 0)
						continue;
				} else {
					/* if ((rematch(argv[optind], de->d_name, REG_EXTENDED)) != 0)*/
					if ((strncmp(argv[optind], de->d_name, len)) != 0)
						continue;
				}
			}
			if (argc < 2)
				printf("%s/%s\n", dentry->d_name, de->d_name);
			else {
				FILE *fp;
				snprintf(buf, sizeof(buf), "/var/db/pkg/%s/%s/CONTENTS",
				         dentry->d_name, de->d_name);
				if ((fp = fopen(buf, "r")) == NULL)
					continue;
				while ((fgets(buf, sizeof(buf), fp)) != NULL) {
					if ((p = strchr(buf, ' ')) == NULL)
						continue;
					++p;
					switch (*buf) {
					case '\n':   /* newline */
						break;
					case 'd':    /* dir */
						/*printf("%s", p);*/
						break;
					case 'o':    /* obj */
					case 's':    /* sym */
						strcpy(buf2, p);
						if ((p = strchr(buf2, ' ')) != NULL)
							*p = 0;
						printf("%s\n", buf2);
						break;
					default:
						break;
					}
				}
				fclose(fp);
			}
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	return 0;
}
