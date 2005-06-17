/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.6 2005/06/17 00:35:11 vapier Exp $
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



#define QFILE_FLAGS "e" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	COMMON_OPTS_HELP
};
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, APPLET_QFILE)



void qfile(char *path, char *fname)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *p, *ptr;
	size_t flen = strlen(fname);
	int base = 0;
	char buf[1024];

	if (chdir(path) != 0 || (dir = opendir(path)) == NULL)
		return;

	if (!strchr(fname, '/'))
		base = 1;
	else
		base = 0;

	while ((dentry = readdir(dir))) {
		if (dentry->d_name[0] == '.')
			continue;
		if (asprintf(&p, "%s/%s/CONTENTS", path, dentry->d_name) == -1)
			continue;
		if ((fp = fopen(p, "r")) == NULL) {
			free(p);
			continue;
		}
		free(p);
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			/* FIXME: Need to port portage_versions.py to c to do this properly. */
			if ((p = strchr(buf, ' ')) == NULL)
				continue;
			*p++;
			ptr = strdup(p);
			if (!ptr)
				continue;
			if ((p = strchr(ptr, '\n')) != NULL)
				*p = '\0';
			if ((p = strchr(ptr, ' ')) != NULL)
				*p++ = 0;
			if (strncmp(base ? basename(ptr) : ptr, fname, flen) != 0
			    || strlen(base ? basename(ptr) : ptr) != flen) {
				free(ptr);
				continue;
			}
			/* If the d_name is something like foo-3DVooDoo-0.0-r0 doing non
			 * exact matches would fail to display the name properly.
			 * /var/cvsroot/gentoo-x86/app-dicts/canna-2ch/
			 * /var/cvsroot/gentoo-x86/net-dialup/intel-536ep/
			 * /var/cvsroot/gentoo-x86/net-misc/cisco-vpnclient-3des/
			 * /var/cvsroot/gentoo-x86/sys-cluster/openmosix-3dmon-stats/
			 * /var/cvsroot/gentoo-x86/sys-cluster/openmosix-3dmon/
			 */
			if (!exact && (p = strchr(dentry->d_name, '-')) != NULL) {
				++p;
				if (*p >= '0' && *p <= '9') {
					--p;
					*p = 0;
				} else {
					/* tricky tricky.. I wish to advance to the second - */
					/* and repeat the first p strchr matching */
					char *q = strdup(p);
					if (!q) {
						free(ptr);
						continue;
					}
					if ((p = strchr(q, '-')) != NULL) {
						int l = 0;
						++p;
						if (*p >= '0' && *p <= '9') {
							--p;
							*p = 0;
							++p;
							l = strlen(dentry->d_name) - strlen(p) - 1;
							dentry->d_name[l] = 0;
						}
					}
					free(q);
				}
			}

			printf("%s%s/%s%s%s (%s)\n", BOLD, basename(path), BLUE, 
			       dentry->d_name, NORM, ptr);

			free(ptr);
			found++;
		}
		fclose(fp);
	}
	closedir(dir);
	return;
}

int qfile_main(int argc, char **argv)
{
	DIR *dir;
	struct dirent *dentry;
	int i;
	char *p;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qfile)
		case 'e': exact = 1;
		}
	}
	if (argc == optind)
		qfile_usage(EXIT_FAILURE);

	if (chdir(portvdb) != 0 || (dir = opendir(portvdb)) == NULL)
		return EXIT_FAILURE;

	/* open /var/db/pkg */
	while ((dentry = readdir(dir)) != NULL) {
		if (dentry->d_name[0] == '.')
			continue;
		for (i = optind; i < argc; ++i) {
			if (asprintf(&p, "%s/%s", portvdb, dentry->d_name) != -1) {
				qfile(p, argv[i]);
				free(p);
			}
		}
	}
	closedir(dir);

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}
