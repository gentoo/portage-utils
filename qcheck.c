/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.6 2005/06/09 17:38:18 solar Exp $
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


#define QCHECK_FLAGS "a" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"all", no_argument, NULL, 'a'},
	COMMON_LONG_OPTS
};
static const char *qcheck_opts_help[] = {
	"List all packages",
	COMMON_OPTS_HELP
};
#define qcheck_usage(ret) usage(ret, QCHECK_FLAGS, qcheck_long_opts, qcheck_opts_help, APPLET_QCHECK)



int qcheck_main(int argc, char **argv)
{
	DIR *dir, *dirp;
	int i;
	struct dirent *dentry, *de;
	char search_all = 0;
	char *cat, *p, *q;
	const char *path = "/var/db/pkg";
	struct stat st;
	size_t num_files, num_files_ok, num_files_unknown;
	size_t len = 0;
	char buf[_POSIX_PATH_MAX];
	char buf2[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = 1; break;
		}
	}
	if ((argc == optind) && (search_all == 0))
		qcheck_usage(EXIT_FAILURE);

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return 1;

	p = q = cat = NULL;
	if (search_all == 0) {
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
				if (search_all == 0)
					if ((strncmp(argv[optind], de->d_name, len)) != 0)
						continue;
			}

			num_files = num_files_ok = num_files_unknown = 0;
			snprintf(buf, sizeof(buf), "/var/db/pkg/%s/%s/CONTENTS",
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;
			if (color)
				printf("Checking " GREE "%s/%s" NORM " ...\n", dentry->d_name, de->d_name);
			else
				printf("Checking %s/%s ...\n", dentry->d_name, de->d_name);
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				char *file, *digest, *mtime;
				file = digest = mtime = NULL;
				if ((p = strchr(buf, ' ')) == NULL)
					continue;
				++p;
				switch (*buf) {
				case '\n':   /* newline */
					break;
				case 'd':    /* dir */
				case 'o':    /* obj */
				case 's':    /* sym */
					strcpy(buf2, p);
					file = buf2;
					if ((p = strchr(file, ' ')) != NULL)
						*p = '\0';
					if (*buf == 'o') {
						digest = p+1;
						if ((mtime = strchr(digest, ' ')) != NULL) {
							*mtime = '\0';
							++mtime;
						}
					}
					if ((p = strchr(file, '\n')) != NULL)
						*p = '\0';

					++num_files;
					if (lstat(file, &st)) {
						if (color)
							printf(" " RED "AFK" NORM ": %s\n", file);
						else
							printf(" AFK: %s\n", file);
					} else {
						if (S_ISREG(st.st_mode)) {
							char *hashed_file;
							hashed_file = hash_file(file, HASH_MD5);
							if ((hashed_file != NULL) && (digest != NULL)) {
								if (strcmp(digest, hashed_file)) {
									if (color)
										printf(" " RED "MD5" NORM ": %s\n", file);
									else
										printf(" MD5: %s\n", file);
								} else
									++num_files_ok;
							} else {
								if (color)
									printf(" " RED "%o" NORM ": %s\n", st.st_mode & 07777, file);
								else
									printf(" %o: %s\n", st.st_mode & 07777, file);
								++num_files_unknown;
							}
						} else
							++num_files_ok;
					}
					break;
				default:
					warnf("Unhandled: '%s'", buf);
					break;
				}
			}
			fclose(fp);
			if (color)
				printf("  " BOLD "* " BLUE "%lu" NORM " out of " BLUE "%lu" NORM " files are good\n",
				       (unsigned long)num_files_ok,
				       (unsigned long)num_files);
			else
				printf("  * %lu out of %lu files are good\n",
				       (unsigned long)num_files_ok,
				       (unsigned long)num_files);
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	return 0;
}
