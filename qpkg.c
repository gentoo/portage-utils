/*
 * Copyright 2005-2006 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/qpkg.c,v 1.22 2007/05/23 03:22:31 solar Exp $
 *
 * Copyright 2005-2006 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2006 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qpkg

#define QPKG_FLAGS "pP:" COMMON_FLAGS
static struct option const qpkg_long_opts[] = {
	{"pretend",  no_argument, NULL, 'p'},
	{"pkgdir",    a_argument, NULL, 'P'},
	COMMON_LONG_OPTS
};
static const char *qpkg_opts_help[] = {
	"pretend only",
	"alternate package directory",
	COMMON_OPTS_HELP
};
static const char qpkg_rcsid[] = "$Id: qpkg.c,v 1.22 2007/05/23 03:22:31 solar Exp $";
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, lookup_applet_idx("qpkg"))

extern char pretend;

static char *qpkg_bindir = NULL;

const char *qpkg_get_bindir(void);
const char *qpkg_get_bindir(void)
{
	if (qpkg_bindir != NULL)
		return qpkg_bindir;
	if (getuid() == 0)
		return "/var/tmp/binpkgs";
	if (getenv("HOME") == NULL)
		errp("Your $HOME env var isn't set, aborting");
	xasprintf(&qpkg_bindir, "%s/binpkgs", getenv("HOME"));

	return qpkg_bindir;
}

int qpkg_make(depend_atom *atom);
int qpkg_make(depend_atom *atom)
{
	FILE *fp, *out;
	char tmpdir[BUFSIZE], filelist[BUFSIZE], xpak[BUFSIZE], tbz2[BUFSIZE];
	char buf[BUFSIZE];
	int i;
	char *xpak_argv[2];
	struct stat st;

	if (pretend) {
		printf(" %s-%s %s/%s:\n", GREEN, NORM, atom->CATEGORY, atom->P);
		return 0;
	}

	snprintf(buf, sizeof(buf), "%s/%s/%s/CONTENTS", portvdb, atom->CATEGORY, atom->P);
	if ((fp = fopen(buf, "r")) == NULL)
		return -1;

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", qpkg_get_bindir());
	if ((i = mkstemp(tmpdir)) == -1)
		return -2;
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750))
		return -3;

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL)
		return -4;

	printf(" %s-%s %s/%s: ", GREEN, NORM, atom->CATEGORY, atom->P);
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

	snprintf(tbz2, sizeof(tbz2), "%s/%s.tbz2", qpkg_get_bindir(), atom->P);
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
	size_t s, pkgs_made;
	int i;
	DIR *dir, *dirp;
	struct dirent *dentry_cat, *dentry_pkg;
	struct stat st;
	char buf[BUFSIZE];
	const char *bindir;
	depend_atom *atom;
	int restrict_chmod = 0;

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	while ((i = GETOPT_LONG(QPKG, qpkg, "")) != -1) {
		switch (i) {
		case 'p': pretend = 1; break;
		case 'P':
			restrict_chmod = 1;
			qpkg_bindir = xstrdup(optarg);
			if ((access(qpkg_bindir, W_OK) != 0))
				errp("%s", qpkg_bindir);
			break;
		COMMON_GETOPTS_CASES(qpkg)
		}
	}
	if (argc == optind)
		qpkg_usage(EXIT_FAILURE);

	if (chdir(portroot))
		errp("could not chdir(%s) for ROOT", portroot);

	/* setup temp dirs */
	i = 0;
	bindir = qpkg_get_bindir();
	if (*bindir != '/')
		err("'%s' is not a valid package destination", bindir);
retry_mkdir:
	if (mkdir(bindir, 0750) == -1) {
		lstat(bindir, &st);
		if (!S_ISDIR(st.st_mode)) {
			unlink(bindir);
			if (!i++) goto retry_mkdir;
			errp("could not create temp bindir '%s'", bindir);
		}
		if (!restrict_chmod)
			if (chmod(bindir, 0750))
				errp("could not chmod(0750) temp bindir '%s'", bindir);
	}

	/* first process any arguments which point to /var/db/pkg */
	pkgs_made = 0;
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
			if (!qpkg_make(atom)) ++pkgs_made;
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
				warn("could not explode '%s'", buf);
				continue;
			}
			snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			for (i = optind; i < argc; ++i) {
				if (!argv[i]) continue;

				if (!strcmp(argv[i], atom->PN) || !strcmp(argv[i], atom->P) || !strcmp(argv[i], buf) || !strcmp(argv[i], "world"))
					if (!qpkg_make(atom)) ++pkgs_made;
			}
			atom_implode(atom);
		}
	}

	s = (argc - optind) - pkgs_made;
	if (s && !pretend)
		printf(" %s*%s %i package%s could not be matched :/\n", RED, NORM, (int)s, (s > 1 ? "s" : ""));
	if (pkgs_made)
		printf(" %s*%s Packages can be found in %s\n", GREEN, NORM, bindir);

	return (pkgs_made ? EXIT_SUCCESS : EXIT_FAILURE);
}

#else
DEFINE_APPLET_STUB(qpkg)
#endif
