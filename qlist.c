/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qlist.c,v 1.5 2005/06/13 23:48:41 vapier Exp $
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
	char *p, *q;
	struct stat st;
	char buf[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QLIST, qlist, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qlist)
		}
	}

	if (chdir(portvdb) != 0 || (dir = opendir(portvdb)) == NULL)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	while ((dentry = readdir(dir))) {
		/* search for a category directory */
		if (dentry->d_name[0] == '.')
			continue;
		if (strchr(dentry->d_name, '-') == NULL)
			continue;
		stat(dentry->d_name, &st);
		if (!S_ISDIR(st.st_mode))
			continue;
		if (chdir(dentry->d_name) != 0)
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;

		/* open the cateogry */
		while ((de = readdir(dirp)) != NULL) {
			FILE *fp;
			if (*de->d_name == '.')
				continue;

			/* see if this cat/pkg is requested */
			for (i = optind; i < argc; ++i) {
				snprintf(buf, sizeof(buf), "%s/%s", dentry->d_name, 
				         de->d_name);
				if (rematch(argv[i], buf, REG_EXTENDED) == 0)
					break;
				if (rematch(argv[i], de->d_name, REG_EXTENDED) == 0)
					break;
			}
			if (i == argc)
				continue;

			snprintf(buf, sizeof(buf), "%s/%s/%s/CONTENTS", portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;

			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				if ((p = strchr(buf, '\n')) != NULL)
					*p = '\0';
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
					if ((q = strchr(p, ' ')) != NULL)
						*q = '\0';
					printf("%s\n", p);
					break;
				default:
					break;
				}
			}
			fclose(fp);
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	return 0;
}
