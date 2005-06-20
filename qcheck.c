/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qcheck.c,v 1.11 2005/06/20 20:17:22 solar Exp $
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


#define QCHECK_FLAGS "ar:" COMMON_FLAGS
static struct option const qcheck_long_opts[] = {
	{"all",  no_argument, NULL, 'a'},
	{"root", no_argument, NULL, 'r'},
	COMMON_LONG_OPTS
};
static const char *qcheck_opts_help[] = {
	"List all packages",
	"Chroot before package verification",
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
	char *p;
	char buf[_POSIX_PATH_MAX], root[_POSIX_PATH_MAX] = "";

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QCHECK, qcheck, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qcheck)
		case 'a': search_all = 1; break;
		case 'r': strncpy(root, optarg, sizeof(root)); break;
		}
	}
	if ((argc == optind) && !search_all)
		qcheck_usage(EXIT_FAILURE);

	/* cheap trick for now */
	if (*root) {
		if (chroot(root) != 0) {
			warn("failed to chroot to '%s' : %s", root, strerror(errno));
			return EXIT_FAILURE;
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

			num_files = num_files_ok = num_files_unknown = 0;
			printf("Checking %s%s/%s%s ...\n", GREEN, dentry->d_name, de->d_name, NORM);
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
				case 'o':    /* obj */
				case 's': {  /* sym */
					char *file, *digest, *mtime_ascii;
					long mtime;

					/* parse the CONTENTS file */
					file = p;
					digest = mtime_ascii = NULL;
					mtime = 0;
					if ((p = strchr(file, ' ')) != NULL)
						*p = '\0';
					if (*buf == 'o' && p) {
						digest = p+1;
						if ((mtime_ascii = strchr(digest, ' ')) != NULL) {
							*mtime_ascii = '\0';
							++mtime_ascii;
							mtime = strtol(mtime_ascii, NULL, 10);
							if (mtime == LONG_MAX) {
								warn("Invalid mtime '%s'", mtime_ascii);
								mtime = 0;
								mtime_ascii = NULL;
							}
						}
					}

					/* run our little checks */
					++num_files;
					if (lstat(file, &st)) {
						/* make sure file exists */
						printf(" %sAFK%s: %s\n", RED, NORM, file);
						break;
					}
					if (digest && S_ISREG(st.st_mode)) {
						/* validate digest (handles MD5 / SHA1) */
						uint8_t hash_algo;
						char *hashed_file;
						switch (strlen(digest)) {
							case 32: hash_algo = HASH_MD5; break;
							case 40: hash_algo = HASH_SHA1; break;
							default: hash_algo = 0; break;
						}
						if (!hash_algo) {
							printf(" %sUNKNOWN DIGEST%s: '%s' for '%s'\n", RED, NORM, digest, file);
							++num_files_unknown;
							break;
						}
						hashed_file = hash_file(file, hash_algo);
						if (!hashed_file) {
							printf(" %sPERM %4o%s: %s\n", RED, (st.st_mode & 07777), NORM, file);
							++num_files_unknown;
							break;
						} else if (strcmp(digest, hashed_file)) {
							const char *digest_disp;
							switch (hash_algo) {
								case HASH_MD5:  digest_disp = "MD5"; break;
								case HASH_SHA1: digest_disp = "SHA1"; break;
								default:        digest_disp = "UNK"; break;
							}
							printf(" %s%s-DIGEST%s: %s\n", RED, digest_disp, NORM, file);
							break;
						}
					}
					if (mtime && mtime != st.st_mtime) {
						/* validate last modification time */
						printf(" %sMTIME%s: %s\n", RED, NORM, file);
						break;
					}
					++num_files_ok;
					break;
				}
				default:
					warnf("Unhandled: '%s'", buf);
					break;
				}
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
