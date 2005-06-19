/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.8 2005/06/19 04:54:15 solar Exp $
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



#define QFILE_FLAGS "eq" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	{"quiet",       no_argument, NULL, 'q'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	"Output package only",
	COMMON_OPTS_HELP
};
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, APPLET_QFILE)

static short qfile_quiet = 0;

void qfile(char *path, char *fname)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *p, *ptr;
	size_t flen = strlen(fname);
	int base = 0;
	char buf[1024];
	char pkg[126];
	depend_atom *atom;

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

		snprintf(pkg, sizeof(pkg), "%s/%s", basename(path), dentry->d_name);
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			if ((p = strchr(buf, ' ')) == NULL)
				continue;
			*p++;
			if ((ptr = strdup(p)) == NULL)
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
			if ((atom = atom_explode(pkg)) == NULL) {
				warn("invalid atom %s", pkg);
				continue;
			}
			printf("%s%s/%s%s%s", BOLD, atom->CATEGORY, BLUE,
				(exact ? dentry->d_name : atom->PN), NORM);

			if (qfile_quiet == 1)
				puts("");
			else
				printf(" (%s)\n", ptr);

			atom_free(atom);
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

	qfile_quiet = 0;

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'q': qfile_quiet = 1; break;
			case 'e': exact = 1; break;
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
