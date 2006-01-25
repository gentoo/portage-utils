/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qfile.c,v 1.25 2006/01/25 01:51:42 vapier Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qfile

#define QFILE_FLAGS "e" COMMON_FLAGS
static struct option const qfile_long_opts[] = {
	{"exact",       no_argument, NULL, 'e'},
	COMMON_LONG_OPTS
};
static const char *qfile_opts_help[] = {
	"Exact match",
	COMMON_OPTS_HELP
};
static char qfile_rcsid[] = "$Id: qfile.c,v 1.25 2006/01/25 01:51:42 vapier Exp $";
#define qfile_usage(ret) usage(ret, QFILE_FLAGS, qfile_long_opts, qfile_opts_help, lookup_applet_idx("qfile"))

void qfile(char *path, char *fullname);
void qfile(char *path, char *fullname)
{
	FILE *fp;
	DIR *dir;
	struct dirent *dentry;
	char *p;
	size_t flen;
	int base = 0;
	char fname[_Q_PATH_MAX];
	char buf[1024];
	char pkg[126];
	depend_atom *atom;

	strncpy(fname, fullname, sizeof(fname));

	if ((fname[0] == '.') && ((p = getenv("PWD")) != NULL)) {
		char tmp[PATH_MAX];
		snprintf(tmp, sizeof(fname), "%s/%s", p, fullname);
		errno = 0;
		realpath(tmp, fname);
		assert(errno == 0);
	}

	flen = strlen(fname);

	if (chdir(path) != 0 || (dir = opendir(".")) == NULL)
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

			if (quiet)
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

	while ((i = GETOPT_LONG(QFILE, qfile, "")) != -1) {
		switch (i) {
			COMMON_GETOPTS_CASES(qfile)
			case 'e': exact = 1; break;
		}
	}
	if (!exact && verbose) exact++;
	if (argc == optind)
		qfile_usage(EXIT_FAILURE);

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	if (chdir(portvdb) != 0 || (dir = opendir(".")) == NULL)
		return EXIT_FAILURE;

	/* CONTENTS stores dir names w/out trailing / so clean up input */
	for (i = optind; i < argc; ++i) {
		p = argv[i] + strlen(argv[i]) - 1;
		if (*p == '/')
			*p = '\0';
	}

	/* open /var/db/pkg */
	while ((dentry = q_vdb_get_next_dir(dir))) {
		xasprintf(&p, "%s%s/%s", portroot, portvdb, dentry->d_name);
		for (i = optind; i < argc; ++i)
			qfile(p, argv[i]);
		free(p);
	}

	return (found ? EXIT_SUCCESS : EXIT_FAILURE);
}

#endif
