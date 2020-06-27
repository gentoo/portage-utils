/*
 * Copyright 2005-2020 Gentoo Foundation
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
#include "hash.h"
#include "human_readable.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xarray.h"
#include "xasprintf.h"
#include "xchdir.h"
#include "xmkdir.h"
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
	"clean pkgdir of files that are not installed",
	"clean pkgdir of files that are not in the tree anymore",
	"pretend only",
	"alternate package directory",
	COMMON_OPTS_HELP
};
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, NULL, lookup_applet_idx("qpkg"))

extern char pretend;

static char *qpkg_bindir = NULL;
static int eclean = 0;

/* figure out what dirs we want to process for cleaning and display results. */
static int
qpkg_clean(char *dirp)
{
	size_t n;
	size_t disp_units = 0;
	uint64_t num_all_bytes = 0;
	set *known_pkgs = NULL;
	set *bin_pkgs = NULL;
	DECLARE_ARRAY(bins);
	tree_ctx *t;
	tree_ctx *pkgs;
	char *binatomstr;
	depend_atom *atom;
	char buf[_Q_PATH_MAX];
	struct stat st;

	pkgs = tree_open_binpkg(portroot, dirp);
	if (pkgs == NULL)
		return 1;

	bin_pkgs = tree_get_atoms(pkgs, true, bin_pkgs);
	array_set(bin_pkgs, bins);

	if (eclean) {
		const char *overlay;

		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL) {
				known_pkgs = tree_get_atoms(t, true, known_pkgs);
				tree_close(t);
			}
		}
	} else {
		t = tree_open_vdb(portroot, portvdb);
		if (t != NULL) {
			known_pkgs = tree_get_atoms(t, true, known_pkgs);
			tree_close(t);
		}
	}

	if (known_pkgs != NULL) {
		/* check which binpkgs exist in the known_pkgs (vdb or trees), such
		 * that the remainder is what we would clean */
		array_for_each(bins, n, binatomstr) {
			if (contains_set(binatomstr, known_pkgs))
				xarraydelete_ptr(bins, n--);
		}

		free_set(known_pkgs);
	}

	array_for_each(bins, n, binatomstr) {
		snprintf(buf, sizeof(buf), "%s/%s.tbz2", dirp, binatomstr);
		atom = atom_explode(binatomstr);
		if (lstat(buf, &st) != -1) {
			if (S_ISREG(st.st_mode)) {
				disp_units = KILOBYTE;
				if ((st.st_size / KILOBYTE) > 1000)
					disp_units = MEGABYTE;
				num_all_bytes += st.st_size;
				qprintf(" %s[%s %3s %s %s]%s %s\n",
						DKBLUE, GREEN,
						make_human_readable_str(st.st_size, 1, disp_units),
						disp_units == MEGABYTE ? "MiB" : "KiB",
						DKBLUE, NORM, atom_format("%[CAT]/%[PF]", atom));
			}
			if (!pretend)
				unlink(buf);
		}
		atom_implode(atom);
	}

	xarrayfree_int(bins);
	free_set(bin_pkgs);

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
	mode_t mask;

	if (pretend) {
		printf(" %s-%s %s:\n",
				GREEN, NORM, atom_format("%[CATEGORY]%[PF]", atom));
		return 0;
	}

	buflen = _Q_PATH_MAX;
	buf = xmalloc(buflen);

	snprintf(buf, buflen, "%s/%s/%s/CONTENTS",
			portvdb, atom->CATEGORY, atom->PF);
	if ((fp = fopen(buf, "r")) == NULL) {
		free(buf);
		return -1;
	}

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", qpkg_bindir);
	mask = umask(0077);
	i = mkstemp(tmpdir);
	umask(mask);
	if (i == -1) {
		fclose(fp);
		free(buf);
		return -2;
	}
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750)) {
		fclose(fp);
		free(buf);
		return -3;
	}

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL) {
		fclose(fp);
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
		fprintf(out, "%s\n", e->name+1); /* don't output leading / */
		if (e->type == CONTENTS_OBJ && verbose) {
			char *hash = hash_file(e->name, HASH_MD5);
			if (hash != NULL) {
				if (strcmp(e->digest, hash) != 0)
					warn("MD5: mismatch expected %s got %s for %s",
							e->digest, hash, e->name);
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

	if ((i = open(tbz2, O_WRONLY)) < 0) {
		warnp("failed to open '%s': %s", tbz2, strerror(errno));
		free(buf);
		return 1;
	}

	/* get offset where xpak will start */
	if (fstat(i, &st) == -1) {
		warnp("could not stat '%s': %s", tbz2, strerror(errno));
		close(i);
		free(buf);
		return 1;
	}
	xpaksize = st.st_size;

	snprintf(buf, buflen, "%s/%s/%s",
			portvdb, atom->CATEGORY, atom->PF);
	xpak_argv[0] = buf;
	xpak_argv[1] = NULL;
	xpak_create(AT_FDCWD, tbz2, 1, xpak_argv, 1, verbose);

	/* calculate the number of bytes taken by the xpak archive */
	if (fstat(i, &st) == -1) {
		warnp("could not stat '%s': %s", tbz2, strerror(errno));
		close(i);
		free(buf);
		return 1;
	}
	xpaksize = st.st_size - xpaksize;

	/* save tbz2 tail: OOOOSTOP */
	if ((fp = fdopen(i, "a")) == NULL) {
		warnp("could not open '%s': %s", tbz2, strerror(errno));
		close(i);
		free(buf);
		return 1;
	}

	WRITE_BE_INT32(buf, xpaksize);
	fwrite(buf, 1, 4, fp);
	fwrite("STOP", 1, 4, fp);
	fclose(fp);

	unlink(filelist);

	/* create dirs, if necessary */
	snprintf(buf, buflen, "%s/%s", qpkg_bindir, atom->CATEGORY);
	mkdir_p(buf, 0755);

	snprintf(buf, buflen, "%s/%s/%s.tbz2",
			qpkg_bindir, atom->CATEGORY, atom->PF);
	if (rename(tbz2, buf)) {
		warnp("could not move '%s' to '%s'", tbz2, buf);
		free(buf);
		return 1;
	}

	rmdir(tmpdir);

	if (stat(buf, &st) == -1) {
		warnp("could not stat '%s': %s", buf, strerror(errno));
		free(buf);
		return 1;
	}

	printf("%s%s%s KiB\n",
			RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	free(buf);
	return 0;
}

static int
qpkg_cb(tree_pkg_ctx *pkg, void *priv)
{
	size_t *pkgs_made = priv;

	if (qpkg_make(tree_get_atom(pkg, false)) == 0)
		(*pkgs_made)++;

	return 0;
}

int qpkg_main(int argc, char **argv)
{
	tree_ctx *ctx;
	size_t s, pkgs_made;
	int i;
	struct stat st;
	depend_atom *atom;
	int restrict_chmod = 0;
	int qclean = 0;
	int fd;

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
	/* brute force just unlink any file or symlink, if this fails, it's
	 * actually good :) */
	unlink(qpkg_bindir);
	fd = open(qpkg_bindir, O_RDONLY);
	if ((fd == -1 && mkdir(qpkg_bindir, 0750) == -1) ||
			(fd != -1 && (fstat(fd, &st) == -1 || !S_ISDIR(st.st_mode))))
	{
		errp("could not create temp bindir '%s'", qpkg_bindir);
	}
	if (fd >= 0) {
		/* fd is valid, pointing to a directory */
		if (!restrict_chmod)
			if (fchmod(fd, 0750) < 0)
				errp("could not chmod(0750) temp bindir '%s'", qpkg_bindir);
		close(fd);
	}

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

	for (i = optind; i < argc; ++i) {
		if (argv[i] == NULL)
			continue;
		if (strcmp(argv[i], "world") == 0) {
			/* we're basically done, this means all */
			tree_foreach_pkg_fast(ctx, qpkg_cb, &pkgs_made, NULL);
		}
		atom = atom_explode(argv[i]);
		if (atom == NULL)
			continue;

		tree_foreach_pkg_fast(ctx, qpkg_cb, &pkgs_made, atom);
		atom_implode(atom);
	}
	tree_close(ctx);

	if (pkgs_made)
		qprintf(" %s*%s Packages can be found in %s\n",
				GREEN, NORM, qpkg_bindir);

	return (pkgs_made ? EXIT_SUCCESS : EXIT_FAILURE);
}
