/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsize.c,v 1.8 2005/06/13 23:52:56 vapier Exp $
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



#define QSIZE_FLAGS "fasSmkb" COMMON_FLAGS
static struct option const qsize_long_opts[] = {
	{"filesystem", no_argument, NULL, 'f'},
	{"all",        no_argument, NULL, 'a'},
	{"sum",        no_argument, NULL, 's'},
	{"sum-only",   no_argument, NULL, 'S'},
	{"megabytes",  no_argument, NULL, 'm'},
	{"kilobytes",  no_argument, NULL, 'k'},
	{"bytes",      no_argument, NULL, 'b'},
	COMMON_LONG_OPTS
};
static const char *qsize_opts_help[] = {
	"Show size used on disk",
	"Size all installed packages",
	"Include a summary",
	"Show just the summary",
	"Display size in megabytes",
	"Display size in kilobytes",
	"Display size in bytes",
	COMMON_OPTS_HELP
};
#define qsize_usage(ret) usage(ret, QSIZE_FLAGS, qsize_long_opts, qsize_opts_help, APPLET_QSIZE)



int qsize_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	struct dirent *dentry, *de;
	char search_all = 0;
	char *p, *q;
	struct stat st;
	char fs_size = 0, summary = 0, summary_only = 0;
	size_t num_all_bytes, num_all_files, num_all_nonfiles;
	size_t num_bytes, num_files, num_nonfiles;
	size_t disp_units = 0;
	const char *str_disp_units = NULL;
	char buf[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': fs_size = 1; break;
		case 'a': search_all = 1; break;
		case 's': summary = 1; break;
		case 'S': summary = summary_only = 1; break;
		case 'm': disp_units = MEGABYTE; str_disp_units = "MB"; break;
		case 'k': disp_units = KILOBYTE; str_disp_units = "KB"; break;
		case 'b': disp_units = 1; str_disp_units = "bytes"; break;
		}
	}
	if ((argc == optind) && !search_all)
		qsize_usage(EXIT_FAILURE);

	if (chdir(portvdb) != 0 || (dir = opendir(portvdb)) == NULL)
		return EXIT_FAILURE;

	num_all_bytes = num_all_files = num_all_nonfiles = 0;

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
			if (!search_all) {
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
			}

			snprintf(buf, sizeof(buf), "%s/%s/%s/CONTENTS", portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;

			num_files = num_nonfiles = num_bytes = 0;
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				if ((p = strchr(buf, '\n')) != NULL)
					*p = '\0';
				if ((p = strchr(buf, ' ')) == NULL)
					continue;
				++p;
				switch (*buf) {
				case '\n':   /* newline */
					break;
				case 'o':    /* obj */
				case 's':    /* sym */
					if ((q = strchr(p, ' ')) != NULL)
						*q = '\0';
					++num_files;
					if (!lstat(p, &st))
						num_bytes += (fs_size ? st.st_blocks * S_BLKSIZE : st.st_size);
					break;
				default:
					++num_nonfiles;
					break;
				}
			}
			fclose(fp);
			num_all_bytes += num_bytes;
			num_all_files += num_files;
			num_all_nonfiles += num_nonfiles;
			if (!summary_only) {
				printf("%s%s/%s%s%s: %lu files, %lu non-files, ", BOLD, 
				       basename(dentry->d_name), BLUE, de->d_name, NORM,
				       (unsigned long)num_files, 
				       (unsigned long)num_nonfiles);
				if (disp_units)
					printf("%s %s\n",
					       make_human_readable_str(num_bytes, 1, disp_units),
					       str_disp_units);
				else
					printf("%lu.%lu KB\n",
					       (unsigned long)(num_bytes / KILOBYTE),
					       (unsigned long)(((num_bytes%KILOBYTE)*1000)/KILOBYTE));
			}
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	if (summary) {
		printf(" %sTotals%s: %lu files, %lu non-files, ", BOLD, NORM,
		       (unsigned long)num_all_files,
		       (unsigned long)num_all_nonfiles);
		if (disp_units)
			printf("%s %s\n",
			       make_human_readable_str(num_all_bytes, 1, disp_units),
			       str_disp_units);
		else
			printf("%lu.%lu MB\n",
			       (unsigned long)(num_all_bytes / MEGABYTE),
			       (unsigned long)(((num_all_bytes%MEGABYTE)*1000)/MEGABYTE));
	}
	return 0;
}
