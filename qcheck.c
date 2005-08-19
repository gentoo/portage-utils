/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.14 2005/08/19 03:43:56 vapier Exp $
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
	{"all",  no_argument, NULL, 'a'},
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
	struct stat st;
	size_t num_files, num_files_ok, num_files_unknown;
	char buf[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = 1; break;
		}
	}
	if ((argc == optind) && !search_all)
		qcheck_usage(EXIT_FAILURE);

	if (chdir(portvdb) != 0 || (dir = opendir(".")) == NULL)
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

			snprintf(buf, sizeof(buf), "%s%s/%s/%s/CONTENTS", portroot, portvdb,
			         dentry->d_name, de->d_name);
			if ((fp = fopen(buf, "r")) == NULL)
				continue;

			num_files = num_files_ok = num_files_unknown = 0;
			printf("Checking %s%s/%s%s ...\n", GREEN, dentry->d_name, de->d_name, NORM);
			while ((fgets(buf, sizeof(buf), fp)) != NULL) {
				contents_entry *e;

				e = contents_parse_line(buf);
				if (!e)
					continue;

				/* run our little checks */
				++num_files;
				if (lstat(e->name, &st)) {
					/* make sure file exists */
					printf(" %sAFK%s: %s\n", RED, NORM, e->name);
					continue;
				}
				if (e->digest && S_ISREG(st.st_mode)) {
					/* validate digest (handles MD5 / SHA1) */
					uint8_t hash_algo;
					char *hashed_file;
					switch (strlen(e->digest)) {
						case 32: hash_algo = HASH_MD5; break;
						case 40: hash_algo = HASH_SHA1; break;
						default: hash_algo = 0; break;
					}
					if (!hash_algo) {
						printf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n", RED, NORM, e->digest, e->name);
						++num_files_unknown;
						continue;
					}
					hashed_file = (char*)hash_file(e->name, hash_algo);
					if (!hashed_file) {
						printf(" %sPERM %4o%s: %s\n", RED, (st.st_mode & 07777), NORM, e->name);
						++num_files_unknown;
						continue;
					} else if (strcmp(e->digest, hashed_file)) {
						const char *digest_disp;
						switch (hash_algo) {
							case HASH_MD5:  digest_disp = "MD5"; break;
							case HASH_SHA1: digest_disp = "SHA1"; break;
							default:        digest_disp = "UNK"; break;
						}
						printf(" %s%s-DIGEST%s: %s", RED, digest_disp, NORM, e->name);
						if (verbose)
							printf(" (recorded '%s' != actual '%s')", e->digest, hashed_file);
						printf("\n");
						continue;
					}
				}
				if (e->mtime && e->mtime != st.st_mtime) {
					/* validate last modification time */
					printf(" %sMTIME%s: %s", RED, NORM, e->name);
					if (verbose)
						printf(" (recorded '%lu' != actual '%lu')", (unsigned long)st.st_mtime, e->mtime);
					printf("\n");
					continue;
				}
				++num_files_ok;
			}
			fclose(fp);
			printf("  %2$s*%1$s %3$s%4$lu%1$s out of %3$s%5$lu%1$s file%6$s are good\n",
			       NORM, BOLD, BLUE,
			       (unsigned long)num_files_ok,
			       (unsigned long)num_files,
			       (num_files > 1 ? "s" : ""));
			if (num_files_unknown)
				printf("  %2$s*%1$s Unable to digest %3$s%4$lu%1$s file%5$s\n",
				       NORM, BOLD, BLUE, (unsigned long)num_files_unknown,
				       (num_files_unknown > 1 ? "s" : ""));
		}
		closedir(dirp);
		chdir("..");
	}
	closedir(dir);
	return EXIT_SUCCESS;
}
