/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qsize.c,v 1.4 2005/06/09 01:14:01 vapier Exp $
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
	const char *cat;
	char *p, *q;
	const char *path = "/var/db/pkg";
	struct stat st;
	char fs_size = 0, summary = 0, summary_only = 0;
	size_t num_all_bytes, num_all_files, num_all_nonfiles;
	size_t num_bytes, num_files, num_nonfiles;
	size_t len = 0, disp_units = 0;
	const char *str_disp_units = NULL;
	char buf[_POSIX_PATH_MAX];
	char buf2[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	cat = NULL;
	while ((i = GETOPT_LONG(QSIZE, qsize, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qsize)
		case 'f': fs_size = 1; break;
		case 'a': cat = "*"; break;
		case 's': summary = 1; break;
		case 'S': summary = summary_only = 1; break;
		case 'm': disp_units = MEGABYTE; str_disp_units = "MB"; break;
		case 'k': disp_units = KILOBYTE; str_disp_units = "KB"; break;
		case 'b': disp_units = 1; str_disp_units = "bytes"; break;
		}
	}
	if (argc == optind)
		qsize_usage(EXIT_FAILURE);

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return 1;

	p = q = NULL;
	num_all_bytes = num_all_files = num_all_nonfiles = 0;

	if (cat == NULL) {
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
		if (chdir(dentry->d_name) == (-1))
			continue;
		if ((dirp = opendir(".")) == NULL)
			continue;
		while ((de = readdir(dirp))) {
			FILE *fp;
			if (*de->d_name == '.')
				continue;
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

			num_files = num_nonfiles = num_bytes = 0;
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
				case 'o':    /* obj */
				case 's':    /* sym */
					strcpy(buf2, p);
					if ((p = strchr(buf2, ' ')) != NULL)
						*p = 0;
					++num_files;
					if (!lstat(buf2, &st))
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
				if (color)
					printf(BOLD "%s/" BLUE "%s" NORM, basename(dentry->d_name), de->d_name);
				else
					printf("%s/%s", basename(dentry->d_name), de->d_name);
				printf(": %lu files, %lu non-files, ", num_files, num_nonfiles);
				if (disp_units)
					printf("%s %s\n",
					       make_human_readable_str(num_bytes, 1, disp_units),
					       str_disp_units);
				else
					printf("%lu.%lu KB\n",
					       num_bytes / KILOBYTE,
					       ((num_bytes%KILOBYTE)*1000)/KILOBYTE);
			}
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	if (summary) {
		if (color)
			printf(BOLD " Totals:" NORM);
		else
			printf(" Totals:");
		printf(" %lu files, %lu non-files, ", num_all_files, num_all_nonfiles);
		if (disp_units)
			printf("%s %s\n",
			       make_human_readable_str(num_all_bytes, 1, disp_units),
			       str_disp_units);
		else
			printf("%lu.%lu MB\n",
			       num_all_bytes / MEGABYTE,
			       ((num_all_bytes%MEGABYTE)*1000)/MEGABYTE);
	}
	return 0;
}
