/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.12 2005/07/25 23:32:39 vapier Exp $
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

void qfile(char *path, char *fname);
void qfile(char *path, char *fname)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *p;
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
		xasprintf(&p, "%s/%s/CONTENTS", path, dentry->d_name);
		if ((fp = fopen(p, "r")) == NULL) {
			free(p);
			continue;
		}
		free(p);

		snprintf(pkg, sizeof(pkg), "%s/%s", basename(path), dentry->d_name);
		while ((fgets(buf, sizeof(buf), fp)) != NULL) {
			contents_entry *e;

			e = contents_parse_line(buf);
			if (!e)
				continue;

			p = xstrdup(e->name);
			if (strncmp(base ? basename(p) : p, fname, flen) != 0
			    || strlen(base ? basename(p) : p) != flen) {
				free(p);
				continue;
			}
			if ((atom = atom_explode(pkg)) == NULL) {
				warn("invalid atom %s", pkg);
				free(p);
				continue;
			}
			printf("%s%s/%s%s%s", BOLD, atom->CATEGORY, BLUE,
				(exact ? dentry->d_name : atom->PN), NORM);

			if (qfile_quiet == 1)
				puts("");
			else
				printf(" (%s)\n", p);

			atom_implode(atom);
			free(p);
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

	/* CONTENTS stores dir names w/out trailing / so clean up input */
	for (i = optind; i < argc; ++i) {
		p = argv[i] + strlen(argv[i]) - 1;
		if (*p == '/')
			*p = '\0';
	}

	/* open /var/db/pkg */
	while ((dentry = readdir(dir)) != NULL) {
		if (dentry->d_name[0] == '.')
			continue;
		xasprintf(&p, "%s/%s", portvdb, dentry->d_name);
		for (i = optind; i < argc; ++i)
			qfile(p, argv[i]);
		free(p);
	}
	closedir(dir);

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}
