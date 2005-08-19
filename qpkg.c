/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qpkg.c,v 1.2 2005/08/19 04:14:50 vapier Exp $
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



#define QPKG_FLAGS "" COMMON_FLAGS
static struct option const qpkg_long_opts[] = {
	COMMON_LONG_OPTS
};
static const char *qpkg_opts_help[] = {
	COMMON_OPTS_HELP
};
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, APPLET_QPKG)



#define QPKG_BINDIR "/var/tmp/binpkgs"

int qpkg_make(depend_atom *atom);
int qpkg_make(depend_atom *atom)
{
	FILE *fp, *out;
	char tmpdir[BUFSIZE], filelist[BUFSIZE], xpak[BUFSIZE], tbz2[BUFSIZE];
	char buf[BUFSIZE];
	int i;
	char *xpak_argv[2];
	struct stat st;

	snprintf(buf, sizeof(buf), "%s/%s/%s/CONTENTS", portvdb, atom->CATEGORY, atom->P);
	if ((fp = fopen(buf, "r")) == NULL)
		return -1;

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", QPKG_BINDIR);
	if ((i = mkstemp(tmpdir)) == -1)
		return -2;
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750))
		return -3;

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL)
		return -4;

	printf("   %s-%s %s/%s: ", GREEN, NORM, atom->CATEGORY, atom->P);
	fflush(stdout);

	while ((fgets(buf, sizeof(buf), fp)) != NULL) {
		contents_entry *e;
		e = contents_parse_line(buf);
		if (!e || e->type == CONTENTS_DIR)
			continue;
		fprintf(out, "%s\n", e->name+1); /* dont output leading / */
	}

	fclose(out);
	fclose(fp);

	snprintf(tbz2, sizeof(tbz2), "%s/bin.tar.bz2", tmpdir);
	snprintf(buf, sizeof(buf), "tar jcf '%s' --files-from='%s' --no-recursion &> /dev/null", tbz2, filelist);
	if ((fp = popen(buf, "r")) == NULL)
		return 2;
	pclose(fp);

	snprintf(xpak, sizeof(xpak), "%s/inf.xpak", tmpdir);
	snprintf(buf, sizeof(buf), "%s/%s/%s", portvdb, atom->CATEGORY, atom->P);
	xpak_argv[0] = buf;
	xpak_argv[1] = NULL;
	xpak_create(xpak, 1, xpak_argv);

	snprintf(buf, sizeof(buf), "%s/binpkg.tbz2", tmpdir);
	tbz2_compose(tbz2, xpak, buf);

	unlink(filelist);
	unlink(xpak);
	unlink(tbz2);

	snprintf(tbz2, sizeof(tbz2), "%s/%s.tbz2", QPKG_BINDIR, atom->P);
	if (rename(buf, tbz2)) {
		warnp("could not move '%s' to '%s'", buf, tbz2);
		return 1;
	}

	rmdir(tmpdir);

	stat(tbz2, &st);
	printf("%s%s%s kB\n", RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	return 0;
}

int qpkg_main(int argc, char **argv)
{
	size_t s;
	int i;
	DIR *dir, *dirp;
	struct dirent *dentry_cat, *dentry_pkg;
	struct stat st;
	char buf[BUFSIZE];
	depend_atom *atom;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QPKG, qpkg, "")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(qpkg)
		}
	}
	if (argc == optind)
		qpkg_usage(EXIT_FAILURE);

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	/* setup temp dirs */
	i = 0;
retry_mkdir:
	if (mkdir(QPKG_BINDIR, 0750) == -1) {
		stat(QPKG_BINDIR, &st);
		if (!S_ISDIR(st.st_mode)) {
			unlink(QPKG_BINDIR);
			if (!i++) goto retry_mkdir;
			errp("could not create temp bindir '%s'", QPKG_BINDIR);
		}
		if (chmod(QPKG_BINDIR, 0750))
			errp("could not chmod(0750) temp bindir '%s'", QPKG_BINDIR);
		if (chown(QPKG_BINDIR, 0, 0))
			errp("could not chown(0:0) temp bindir '%s'", QPKG_BINDIR);
	}

	printf(" %s*%s Building packages ...\n", GREEN, NORM);

	/* first process any arguments which point to /var/db/pkg */
	s = strlen(portvdb);
	for (i = optind; i < argc; ++i) {
		size_t asize = strlen(argv[i]);
		if (asize == 0) {
			argv[i] = NULL;
			continue;
		}
		if (argv[i][asize-1] == '/')
			argv[i][asize-1] = '\0';
		if (!strncmp(portvdb, argv[i], s))
			memmove(argv[i], argv[i]+s+1, asize-s);
		else if (argv[i][0] == '/' && !strncmp(portvdb, argv[i]+1, s))
			memmove(argv[i], argv[i]+s+2, asize-s-1);
		else
			continue;

		atom = atom_explode(argv[i]);
		if (atom) {
			qpkg_make(atom);
			atom_implode(atom);
		} else
			warn("could not explode '%s'", argv[i]);
		argv[i] = NULL;
	}

	/* now try to run through vdb and locate matches for user inputs */
	if ((dir = opendir(portvdb)) == NULL)
		return EXIT_FAILURE;

	/* scan all the categories */
	while ((dentry_cat = q_vdb_get_next_dir(dir)) != NULL) {
		snprintf(buf, sizeof(buf), "%s/%s", portvdb, dentry_cat->d_name);
		if ((dirp = opendir(buf)) == NULL)
			continue;

		/* scan all the packages in this category */
		while ((dentry_pkg = q_vdb_get_next_dir(dirp)) != NULL) {

			/* see if user wants any of these packages */
			snprintf(buf, sizeof(buf), "%s/%s", dentry_cat->d_name, dentry_pkg->d_name);
			atom = atom_explode(buf);
			if (!atom) {
				warn("could not explode '%s'", argv[i]);
				continue;
			}

			s = strlen(atom->CATEGORY);
			for (i = optind; i < argc; ++i) {
				if (!argv[i]) continue;

				if (!strcmp(argv[i], atom->PN) || !strcmp(argv[i], atom->P))
					qpkg_make(atom);
			}
			atom_implode(atom);
		}
	}

	printf(" %s*%s Packages can be found in %s\n", GREEN, NORM, QPKG_BINDIR);

	return EXIT_SUCCESS;
}
