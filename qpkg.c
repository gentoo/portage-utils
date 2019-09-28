/*
 * Copyright 2005-2019 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd	       - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2018-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <stdio.h>
#include <string.h>
#include <fnmatch.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "atom.h"
#include "basename.h"
#include "contents.h"
#include "human_readable.h"
#include "md5_sha1_sum.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xarray.h"
#include "xasprintf.h"
#include "xchdir.h"
#include "xpak.h"

#define QPKG_FLAGS "cEpP:" COMMON_FLAGS
static struct option const qpkg_long_opts[] = {
	{"clean",    no_argument, NULL, 'c'},
	{"eclean",   no_argument, NULL, 'E'},
	{"pretend",  no_argument, NULL, 'p'},
	{"pkgdir",    a_argument, NULL, 'P'},
	COMMON_LONG_OPTS
};
static const char * const qpkg_opts_help[] = {
	"clean pkgdir of unused binary files",
	"clean pkgdir of files not in the tree anymore (slow)",
	"pretend only",
	"alternate package directory",
	COMMON_OPTS_HELP
};
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, NULL, lookup_applet_idx("qpkg"))

extern char pretend;

static char *qpkg_bindir = NULL;
static int eclean = 0;

static char *
atom_to_pvr(depend_atom *atom) {
	return (atom->PR_int == 0 ? atom->P : atom->PVR );
}

/* checks to make sure this is a .tbz2 file. used by scandir() */
static int
filter_tbz2(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	if (strlen(dentry->d_name) < 6)
		return 0;
	return !strcmp(".tbz2", dentry->d_name + strlen(dentry->d_name) - 5);
}

/* process a single dir for cleaning. dir can be a $PKGDIR, $PKGDIR/All/, $PKGDIR/$CAT */
static uint64_t
qpkg_clean_dir(char *dirp, set *vdb)
{
	set *ll = NULL;
	struct dirent **fnames;
	int i, count;
	char buf[_Q_PATH_MAX * 2];
	struct stat st;
	uint64_t num_all_bytes = 0;
	size_t disp_units = 0;
	char **t;
	bool ignore;

	if (dirp == NULL)
		return 0;
	if (chdir(dirp) != 0)
		return 0;
	if ((count = scandir(".", &fnames, filter_tbz2, alphasort)) < 0)
		return 0;

	/* create copy of vdb with only basenames */
	for ((void)list_set(vdb, &t); *t != NULL; t++)
		ll = add_set_unique(basename(*t), ll, &ignore);

	for (i = 0; i < count; i++) {
		fnames[i]->d_name[strlen(fnames[i]->d_name)-5] = 0;
		if (contains_set(fnames[i]->d_name, ll))
			continue;
		snprintf(buf, sizeof(buf), "%s.tbz2", fnames[i]->d_name);
		if (lstat(buf, &st) != -1) {
			if (S_ISREG(st.st_mode)) {
				disp_units = KILOBYTE;
				if ((st.st_size / KILOBYTE) > 1000)
					disp_units = MEGABYTE;
				num_all_bytes += st.st_size;
				qprintf(" %s[%s %3s %s %s] %s%s%s\n",
						DKBLUE, GREEN,
						make_human_readable_str(st.st_size, 1, disp_units),
						disp_units == MEGABYTE ? "MiB" : "KiB",
						DKBLUE, BLUE, fnames[i]->d_name, NORM);
			}
			if (!pretend)
				unlink(buf);
		}
	}

	free_set(ll);
	scandir_free(fnames, count);

	return num_all_bytes;
}

/* figure out what dirs we want to process for cleaning and display results. */
static int
qpkg_clean(char *dirp)
{
	int i, count;
	size_t disp_units = 0;
	uint64_t num_all_bytes;
	struct dirent **dnames;
	set *vdb = NULL;
	tree_ctx *t;

	if (chdir(dirp) != 0)
		return 1;
	if ((count = scandir(".", &dnames, filter_hidden, alphasort)) < 0)
		return 1;

	t = tree_open_vdb(portroot, portvdb);
	if (t != NULL) {
		vdb = tree_get_atoms(t, true, vdb);
		tree_close(t);
	}

	if (eclean) {
		size_t n;
		const char *overlay;

		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL) {
				vdb = tree_get_atoms(t, true, vdb);
				tree_close(t);
			}
		}
	}

	num_all_bytes = qpkg_clean_dir(dirp, vdb);

	for (i = 0; i < count; i++) {
		char buf[_Q_PATH_MAX * 2];
		snprintf(buf, sizeof(buf), "%s/%s", dirp, dnames[i]->d_name);
		num_all_bytes += qpkg_clean_dir(buf, vdb);
	}
	scandir_free(dnames, count);

	free_set(vdb);

	disp_units = KILOBYTE;
	if ((num_all_bytes / KILOBYTE) > 1000)
		disp_units = MEGABYTE;
	qprintf(" %s*%s Total space %sfreed in packages "
			"directory: %s%s %ciB%s\n", GREEN, NORM,
			pretend ? "that would be " : "", RED,
			make_human_readable_str(num_all_bytes, 1, disp_units),
			disp_units == MEGABYTE ? 'M' : 'K', NORM);

	return 0;
}

static int
check_pkg_install_mask(char *name)
{
	int i, iargc, ret;
	char **iargv;

	i = iargc = ret = 0;

	if (*name != '/')
		return ret;

	makeargv(pkg_install_mask, &iargc, &iargv);

	for (i = 1; i < iargc; i++) {
		if (fnmatch(iargv[i], name, 0) != 0)
			continue;
		ret = 1;
		break;
	}
	freeargv(iargc, iargv);
	return ret;
}

static int
qpkg_make(depend_atom *atom)
{
	FILE *fp, *out;
	char tmpdir[BUFSIZE];
	char filelist[BUFSIZE + 32];
	char tbz2[BUFSIZE + 32];
	size_t buflen;
	size_t xpaksize;
	char *buf;
	int i;
	char *xpak_argv[2];
	struct stat st;

	if (pretend) {
		printf(" %s-%s %s:\n",
				GREEN, NORM, atom_format("%[CATEGORY]%[PF]", atom));
		return 0;
	}

	buflen = _Q_PATH_MAX;
	buf = xmalloc(buflen);

	snprintf(buf, buflen, "%s/%s/%s/CONTENTS",
			portvdb, atom->CATEGORY, atom_to_pvr(atom));
	if ((fp = fopen(buf, "r")) == NULL) {
		free(buf);
		return -1;
	}

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", qpkg_bindir);
	if ((i = mkstemp(tmpdir)) == -1) {
		free(buf);
		return -2;
	}
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750)) {
		free(buf);
		return -3;
	}

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL) {
		free(buf);
		return -4;
	}

	while (getline(&buf, &buflen, fp) != -1) {
		contents_entry *e;
		e = contents_parse_line(buf);
		if (!e || e->type == CONTENTS_DIR)
			continue;
		if (check_pkg_install_mask(e->name) != 0)
			continue;
		fprintf(out, "%s\n", e->name+1); /* dont output leading / */
		if (e->type == CONTENTS_OBJ && verbose) {
			char *hash = (char *)hash_file(e->name, HASH_MD5);
			if (hash != NULL) {
				if (strcmp(e->digest, hash) != 0)
					warn("MD5: mismatch expected %s got %s for %s",
							e->digest, hash, e->name);
				free(hash);
			}
		}
	}

	fclose(out);
	fclose(fp);

	printf(" %s-%s %s: ", GREEN, NORM,
			atom_format("%[CATEGORY]%[PF]", atom));
	fflush(stdout);

	snprintf(tbz2, sizeof(tbz2), "%s/bin.tbz2", tmpdir);
	if (snprintf(buf, buflen, "tar jcf '%s' --files-from='%s' "
			"--no-recursion >/dev/null 2>&1", tbz2, filelist) > (int)buflen ||
			(fp = popen(buf, "r")) == NULL)
	{
		free(buf);
		return 2;
	}
	pclose(fp);

	/* get offset where xpak will start */
	stat(tbz2, &st);
	xpaksize = st.st_size;

	snprintf(buf, buflen, "%s/%s/%s",
			portvdb, atom->CATEGORY, atom_to_pvr(atom));
	xpak_argv[0] = buf;
	xpak_argv[1] = NULL;
	xpak_create(AT_FDCWD, tbz2, 1, xpak_argv, 1, verbose);

	stat(tbz2, &st);
	xpaksize = st.st_size - xpaksize;

	/* save tbz2 tail: OOOOSTOP */
	fp = fopen(tbz2, "a");
	WRITE_BE_INT32(buf, xpaksize);
	fwrite(buf, 1, 4, fp);
	fwrite("STOP", 1, 4, fp);
	fclose(fp);

	unlink(filelist);

	snprintf(buf, buflen, "%s/%s.tbz2", qpkg_bindir, atom_to_pvr(atom));
	if (rename(tbz2, buf)) {
		warnp("could not move '%s' to '%s'", tbz2, buf);
		free(buf);
		return 1;
	}

	rmdir(tmpdir);

	stat(buf, &st);
	printf("%s%s%s KiB\n",
			RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	free(buf);
	return 0;
}

int qpkg_main(int argc, char **argv)
{
	tree_ctx *ctx;
	tree_cat_ctx *cat_ctx;
	tree_pkg_ctx *pkg_ctx;
	size_t s, pkgs_made;
	int i;
	struct stat st;
	char buf[BUFSIZE];
	depend_atom *atom;
	int restrict_chmod = 0;
	int qclean = 0;

	qpkg_bindir = pkgdir;
	while ((i = GETOPT_LONG(QPKG, qpkg, "")) != -1) {
		switch (i) {
		case 'E': eclean = qclean = 1; break;
		case 'c': qclean = 1; break;
		case 'p': pretend = 1; break;
		case 'P':
			restrict_chmod = 1;
			qpkg_bindir = optarg;
			if (access(qpkg_bindir, W_OK) != 0)
				errp("%s", qpkg_bindir);
			break;
		COMMON_GETOPTS_CASES(qpkg)
		}
	}
	if (qclean)
		return qpkg_clean(qpkg_bindir);

	if (argc == optind)
		qpkg_usage(EXIT_FAILURE);

	/* setup temp dirs */
	if (qpkg_bindir[0] != '/')
		err("'%s' is not a valid package destination", qpkg_bindir);
	for (i = 0; i <= 1; i++) {
		if (mkdir(qpkg_bindir, 0750) == -1) {
			lstat(qpkg_bindir, &st);
			if (!S_ISDIR(st.st_mode)) {
				unlink(qpkg_bindir);
				continue;
			}
			if (!restrict_chmod)
				if (chmod(qpkg_bindir, 0750))
					errp("could not chmod(0750) temp bindir '%s'", qpkg_bindir);
		}
		break;
	}
	if (i == 2)
		errp("could not create temp bindir '%s'", qpkg_bindir);

	/* we have to change to the root so that we can feed the full paths
	 * to tar when we create the binary package. */
	xchdir(portroot);

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
	ctx = tree_open_vdb(portroot, portvdb);
	if (!ctx)
		return EXIT_FAILURE;

	/* scan all the categories */
	while ((cat_ctx = tree_next_cat(ctx))) {
		/* scan all the packages in this category */
		while ((pkg_ctx = tree_next_pkg(cat_ctx))) {
			/* see if user wants any of these packages */
			atom = tree_get_atom(pkg_ctx, false);
			snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			for (i = optind; i < argc; ++i) {
				if (!argv[i]) continue;

				if (!strcmp(argv[i], atom->PN) ||
						!strcmp(argv[i], atom->P) ||
						!strcmp(argv[i], buf) ||
						!strcmp(argv[i], "world"))
					if (!qpkg_make(atom))
						++pkgs_made;
			}
			tree_close_pkg(pkg_ctx);
		}
	}

	s = (argc - optind) - pkgs_made;
	if (s && !pretend)
		printf(" %s*%s %i package%s could not be matched :/\n",
				RED, NORM, (int)s, (s > 1 ? "s" : ""));
	if (pkgs_made)
		qprintf(" %s*%s Packages can be found in %s\n",
				GREEN, NORM, qpkg_bindir);

	return (pkgs_made ? EXIT_SUCCESS : EXIT_FAILURE);
}
