/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd		   - <solar@gentoo.org>
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

#ifdef ENABLE_GPKG
# include <archive.h>
# include <archive_entry.h>
#endif

#include "array.h"
#include "atom.h"
#include "basename.h"
#include "contents.h"
#include "hash.h"
#include "human_readable.h"
#include "scandirat.h"
#include "set.h"
#include "tree.h"
#include "xasprintf.h"
#include "xchdir.h"
#include "xmkdir.h"
#include "xpak.h"

#define QPKG_FLAGS "cEgxpP:" COMMON_FLAGS
static struct option const qpkg_long_opts[] = {
	{"clean",    no_argument, NULL, 'c'},
	{"eclean",   no_argument, NULL, 'E'},
	{"gpkg",     no_argument, NULL, 'g'},
	{"xpak",     no_argument, NULL, 'x'},
	{"pretend",  no_argument, NULL, 'p'},
	{"pkgdir",    a_argument, NULL, 'P'},
	COMMON_LONG_OPTS
};
static const char * const qpkg_opts_help[] = {
	"clean pkgdir of files that are not installed",
	"clean pkgdir of files that are not in the tree anymore",
	"force building of gpkg instead of BINPKG_FORMAT",
	"force building of tbz2/xpak instead of BINPKG_FORMAT",
	"pretend only",
	"alternate package directory",
	COMMON_OPTS_HELP
};
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, NULL, lookup_applet_idx("qpkg"))

extern char pretend;

typedef struct qpkg_cb_args {
	char     *bindir;
	tree_ctx *binpkg;
	tree_ctx *vdb;
	int       clean_notintree:1;
	int       build_gpkg:1;
	size_t    pkgs_made;
} qpkg_cb_args;

/* figure out what dirs we want to process for cleaning and display results. */
static int
qpkg_clean(qpkg_cb_args *args)
{
	size_t n;
	size_t disp_units = 0;
	uint64_t num_all_bytes = 0;
	set *known_pkgs = NULL;
	set *bin_pkgs = NULL;
	array *bins = array_new();
	tree_ctx *t;
	tree_ctx *pkgs;
	char *binatomstr;
	char buf[_Q_PATH_MAX];
	struct stat st;

	pkgs = args->binpkg;
	if (pkgs == NULL)
		return 1;

	bin_pkgs = tree_get_atoms(pkgs, true, bin_pkgs);
	if (bin_pkgs == NULL)
		return 1;
	array_set(bin_pkgs, bins);

	if (args->clean_notintree) {
		const char *overlay;

		array_for_each(overlays, n, overlay) {
			t = tree_open(portroot, overlay);
			if (t != NULL) {
				known_pkgs = tree_get_atoms(t, true, known_pkgs);
				tree_close(t);
			}
		}
	} else {
		t = args->vdb;
		if (t != NULL)
			known_pkgs = tree_get_atoms(t, true, known_pkgs);
	}

	if (known_pkgs != NULL) {
		/* check which binpkgs exist in the known_pkgs (vdb or trees), such
		 * that the remainder is what we would clean */
		array_for_each_rev(bins, n, binatomstr) {
			if (contains_set(binatomstr, known_pkgs))
				array_remove(bins, n);
		}

		free_set(known_pkgs);
	}

	array_for_each(bins, n, binatomstr) {
		depend_atom    *atom = atom_explode(binatomstr);
		tree_match_ctx *m    = tree_match_atom(pkgs, atom, 0);
		if (lstat(m->path, &st) != -1) {
			if (S_ISREG(st.st_mode)) {
				disp_units = KILOBYTE;
				if ((st.st_size / KILOBYTE) > 1000)
					disp_units = MEGABYTE;
				num_all_bytes += st.st_size;
				qprintf(" %s[%s %3s %s %s]%s %s\n",
						DKBLUE, GREEN,
						make_human_readable_str(st.st_size, 1, disp_units),
						disp_units == MEGABYTE ? "MiB" : "KiB",
						DKBLUE, NORM, atom_format("%[CAT]/%[PF]%[BUILDID]",
												  m->atom));
			}
			if (!pretend)
				unlink(buf);
		}
		tree_match_close(m);
		atom_implode(atom);
	}

	array_free(bins);
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

/* this is a simplified version of write_hadhes from qmanifest, maybe
 * one day consolidate the two? */
static void
write_hashes
(
	const char *fname,
	const char *type,
	int         fd
)
{
	size_t flen = 0;
	char sha512[SHA512_DIGEST_LENGTH + 1];
	char blak2b[BLAKE2B_DIGEST_LENGTH + 1];
	char data[8192];
	size_t len;
	const char *name;

	name = strrchr(fname, '/');
	if (name != NULL)
		name++;
	else
		name = "";

	/* this is HASH_DEFAULT, but we still have to set the right buffers,
	 * so do it statically */
	hash_compute_file(fname, NULL, sha512, blak2b, &flen,
					  HASH_SHA512 | HASH_BLAKE2B);

	len = snprintf(data, sizeof(data), "%s %s %zd", type, name, flen);
	len += snprintf(data + len, sizeof(data) - len,
			" SHA512 %s", sha512);
	len += snprintf(data + len, sizeof(data) - len,
			" BLAKE2B %s", blak2b);
	len += snprintf(data + len, sizeof(data) - len, "\n");

	if (write(fd, data, len) != len)
		warnp("failed to write hash data");
}

#ifdef ENABLE_GPKG
static const char *
qgpkg_set_compression(struct archive *a)
{
	/* we compress the metadata and image using zstd as the compression
	 * ratios are close, but the decompression speed is a lot faster,
	 * when unavailable, we go down to xz, bzip2, gzip, lz4 and finally
	 * none */
	if (archive_write_add_filter_zstd(a) == ARCHIVE_OK)
		return ".zst";
	if (archive_write_add_filter_xz(a) == ARCHIVE_OK)
		return ".xz";
	if (archive_write_add_filter_bzip2(a) == ARCHIVE_OK)
		return ".bz2";
	if (archive_write_add_filter_gzip(a) == ARCHIVE_OK)
		return ".gz";
	if (archive_write_add_filter_lz4(a) == ARCHIVE_OK)
		return ".lz4";

	/* none, no filtering */
	return "";
}
#endif

static int
qgpkg_make(tree_pkg_ctx *pkg, qpkg_cb_args *args)
{
#ifdef ENABLE_GPKG
	struct archive *a;
	struct archive_entry *entry;
	struct archive_entry *sparse;
	struct archive_entry_linkresolver *lres;
	struct stat st;
	struct dirent **files = NULL;
	char tmpdir[BUFSIZE];
	char gpkg[BUFSIZE + 32];
	char buf[BUFSIZE * 4];
	char ename[BUFSIZE];
	const char *filter;
	char *line;
	char *savep;
	int i;
	int cnt;
	int dirfd;
	int fd;
	int mfd;
	mode_t mask;
	depend_atom *atom = tree_get_atom(pkg, false);
	ssize_t len;

	if (pretend) {
		printf(" %s-%s %s:\n",
				GREEN, NORM, atom_format("%[CATEGORY]%[PF]%[BUILDID]", atom));
		return 0;
	}

	line = tree_pkg_meta_get(pkg, CONTENTS);
	if (line == NULL)
		return -1;

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", args->binpkg->path);
	mask = umask(S_IRWXG | S_IRWXO);
	i = mkstemp(tmpdir);
	umask(mask);
	if (i == -1)
		return -2;
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750))
		return -3;

	printf(" %s-%s %s: ", GREEN, NORM,
		   atom_format("%[CATEGORY]%[PF]%[BUILDID]", atom));
	fflush(stdout);

	snprintf(buf, sizeof(buf), "%s/Manifest", tmpdir);
	mfd = open(buf, O_WRONLY | O_CREAT | O_TRUNC,
			   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (mfd < 0) {
		rmdir(tmpdir);
		printf("%sFAIL%s\n", RED, NORM);
		return -4;
	}

	snprintf(buf, sizeof(buf), "%s/gpkg-1", tmpdir);
	fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (mfd < 0) {
		close(mfd);
		rm_rf(tmpdir);
		printf("%sFAIL%s\n", RED, NORM);
		return -5;
	}
	/* contractually we don't have to put anything in here, but we drop
	 * our signature so it can be traced back to us */
	len = snprintf(ename, sizeof(ename), "portage-utils-%s", VERSION);
	if (write(fd, ename, (size_t)len) != len)
		warnp("could not write self-identifier");
	close(fd);
	write_hashes(buf, "DATA", mfd);

	/* we first 1. create metadata (vdb), 2. image (actual data) and
	 * then 3. the container gpkg image */

	/* 1. VDB into metadata.tar.zst */
	a = archive_write_new();
	archive_write_set_format_ustar(a);  /* as required by GLEP-78 */
	filter = qgpkg_set_compression(a);
	snprintf(gpkg, sizeof(gpkg), "%s/metadata.tar%s", tmpdir, filter);
	archive_write_open_filename(a, gpkg);

	snprintf(buf, sizeof(buf), "%s/%s/%s",
			args->vdb->path, atom->CATEGORY, atom->PF);
	cnt = 0;
	if ((dirfd = open(buf, O_RDONLY)) >= 0)
		cnt = scandirat(dirfd, ".", &files, filter_self_parent, alphasort);
	for (i = 0; i < cnt; i++) {
		if ((fd = openat(dirfd, files[i]->d_name, O_RDONLY)) < 0)
			continue;
		if (fstat(fd, &st) < 0 || !(st.st_mode & S_IFREG)) {
			close(fd);
			continue;
		}

		entry = archive_entry_new();
		snprintf(ename, sizeof(ename), "metadata/%s", files[i]->d_name);
		archive_entry_set_pathname(entry, ename);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, st.st_mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		close(fd);
		archive_entry_free(entry);
	}
	if (atom->BUILDID > 0) {
		entry = archive_entry_new();
		archive_entry_set_pathname(entry, "metadata/BUILD_ID");
		len = snprintf(ename, sizeof(ename), "%u\n", atom->BUILDID);
		archive_entry_set_size(entry, (size_t)len);
		archive_entry_set_mtime(entry, time(NULL), 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		archive_write_data(a, ename, (size_t)len);
		archive_entry_free(entry);
	}
	archive_write_close(a);
	archive_write_free(a);
	scandir_free(files, cnt);
	if (dirfd >= 0)
		close(dirfd);
	write_hashes(gpkg, "DATA", mfd);

	/* 2. the actual files into image.tar.zst */
	a = archive_write_new();
	archive_write_set_format_ustar(a);  /* as required by GLEP-78 */
	filter = qgpkg_set_compression(a);
	snprintf(gpkg, sizeof(gpkg), "%s/image.tar%s", tmpdir, filter);
	archive_write_open_filename(a, gpkg);
	lres = archive_entry_linkresolver_new();
	archive_entry_linkresolver_set_strategy(lres, archive_format(a));
	for (; (line = strtok_r(line, "\n", &savep)) != NULL; line = NULL) {
		contents_entry *e;
		e = contents_parse_line(line);
		if (!e)
			continue;
		if (check_pkg_install_mask(e->name) != 0)
			continue;
		if (e->name[0] == '/')
			e->name++;
		switch (e->type) {
			case CONTENTS_OBJ:
				if (verbose) {
					char *hash = hash_file_at(args->vdb->portroot_fd,
											  e->name, HASH_MD5);
					if (hash != NULL) {
						if (strcmp(e->digest, hash) != 0)
							warn("MD5 mismatch: expected %s got %s for %s",
									e->digest, hash, e->name);
					}
				}

				if ((fd = openat(args->vdb->portroot_fd,
								 e->name, O_RDONLY)) < 0)
					continue;
				if (fstat(fd, &st) < 0) {
					close(fd);
					continue;
				}

				entry = archive_entry_new();
				snprintf(ename, sizeof(ename), "image/%s", e->name);
				archive_entry_set_pathname(entry, ename);
				archive_entry_copy_stat(entry, &st);
				archive_entry_linkify(lres, &entry, &sparse);
				archive_write_header(a, entry);
				if (sparse != NULL) {
					archive_write_header(a, sparse);
					archive_entry_free(entry);
					entry = sparse;
				}
				if (archive_entry_size(entry) > 0)
				{
					while ((len = read(fd, buf, sizeof(buf))) > 0)
						archive_write_data(a, buf, (size_t)len);
				}
				archive_entry_free(entry);
				close(fd);
				break;
			case CONTENTS_SYM:
				/* like for files, we take whatever is in the filesystem */
				if ((len = readlinkat(args->vdb->portroot_fd,
									  e->name, ename, sizeof(ename) - 1)) < 0)
					snprintf(ename, sizeof(ename), "%s", e->sym_target);
				else
					ename[len] = '\0';

				if (verbose) {
					if (strcmp(e->sym_target, ename) != 0)
						warn("symlink target mismatch: "
							 "expected %s got %s for %s",
							 e->sym_target, ename, e->name);
				}

				entry = archive_entry_new();
				archive_entry_set_symlink(entry, ename);
				snprintf(ename, sizeof(ename), "image/%s", e->name);
				archive_entry_set_pathname(entry, ename);
				if (fstatat(args->vdb->portroot_fd,
							e->name, &st, AT_SYMLINK_NOFOLLOW) < 0)
				{
					archive_entry_set_mtime(entry, e->mtime, 0);
					archive_entry_set_filetype(entry, AE_IFLNK);
					archive_entry_set_mode(entry, 0777);
				} else {
					archive_entry_copy_stat(entry, &st);
				}
				archive_write_header(a, entry);
				archive_entry_free(entry);
				break;
			case CONTENTS_DIR:
				if ((fd = openat(args->vdb->portroot_fd,
								 e->name, O_RDONLY)) < 0)
					continue;
				if (fstat(fd, &st) < 0) {
					close(fd);
					continue;
				}
				close(fd);

				entry = archive_entry_new();
				snprintf(ename, sizeof(ename), "image/%s", e->name);
				archive_entry_set_pathname(entry, ename);
				archive_entry_copy_stat(entry, &st);
				archive_write_header(a, entry);
				archive_entry_free(entry);
				break;
		}
	}
	do {
		const char *fname;

		entry = NULL;
		archive_entry_linkify(lres, &entry, &sparse);
		if (entry == NULL)
			break;

		/* these are hardlinks which apparently have targets outside of
		 * the package's know filelist, e.g. the user adding a hardlink,
		 * we need to process them now depending on the archive format,
		 * which means we'll have to lookup the files to get their body */
		fname = archive_entry_pathname(entry);
		if (fname == NULL ||
			(fname = strchr(fname, '/')) == NULL)
			continue;  /* not something we would've produced */
		fname++;

		if ((fd = openat(args->vdb->portroot_fd, fname, O_RDONLY)) < 0)
			continue;

		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		archive_entry_free(entry);
		close(fd);
	} while (true);
	archive_entry_linkresolver_free(lres);
	archive_write_close(a);
	archive_write_free(a);
	write_hashes(gpkg, "DATA", mfd);

	/* 3. the final gpkg file (to be renamed properly when it all
	 * succeeds */
	snprintf(gpkg, sizeof(gpkg), "%s/bin.gpkg.tar", tmpdir);
	a = archive_write_new();
	archive_write_set_format_ustar(a);  /* as required by GLEP-78 */
	archive_write_open_filename(a, gpkg);

	if (atom->BUILDID > 0)
		i = snprintf(ename, sizeof(ename), "%s-%u", atom->PF, atom->BUILDID);
	else
		i = snprintf(ename, sizeof(ename), "%s", atom->PF);

	/* 3.1 the package format identifier file gpkg-1 */
	snprintf(buf, sizeof(buf), "%s/gpkg-1", tmpdir);
	if ((fd = open(buf, O_RDONLY)) >= 0 &&
		fstat(fd, &st) >= 0)
	{
		entry = archive_entry_new();
		snprintf(ename + i, sizeof(ename) - i, "/gpkg-1");
		archive_entry_set_pathname(entry, ename);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, st.st_mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		close(fd);
		archive_entry_free(entry);
	}
	/* 3.2 the metadata archive metadata.tar${comp} */
	snprintf(buf, sizeof(buf), "%s/metadata.tar%s", tmpdir, filter);
	/* this must succeed, no? */
	if ((fd = open(buf, O_RDONLY)) >= 0 &&
		fstat(fd, &st) >= 0)
	{
		entry = archive_entry_new();
		snprintf(ename + i, sizeof(ename) - i, "/metadata.tar%s", filter);
		archive_entry_set_pathname(entry, ename);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, st.st_mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		close(fd);
		archive_entry_free(entry);
	}
	/* 3.3 TODO: with gpgme write metadata signature */
	/* 3.4 the filesystem image archive image.tar${comp} */
	snprintf(buf, sizeof(buf), "%s/image.tar%s", tmpdir, filter);
	/* this must succeed, no? */
	if ((fd = open(buf, O_RDONLY)) >= 0 &&
		fstat(fd, &st) >= 0)
	{
		entry = archive_entry_new();
		snprintf(ename + i, sizeof(ename) - i, "/image.tar%s", filter);
		archive_entry_set_pathname(entry, ename);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, st.st_mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		close(fd);
		archive_entry_free(entry);
	}
	/* 3.5 TODO: with gpgme write image signature */
	/* 3.6 the package Manifest data file Manifest (clear-signed when
	 * gpgme) */
	close(mfd);
	snprintf(buf, sizeof(buf), "%s/Manifest", tmpdir);
	if ((fd = open(buf, O_RDONLY)) >= 0 &&
		fstat(fd, &st) >= 0)
	{
		entry = archive_entry_new();
		snprintf(ename + i, sizeof(ename) - i, "/Manifest");
		archive_entry_set_pathname(entry, ename);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, st.st_mtime, 0);
		archive_entry_set_filetype(entry, AE_IFREG);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((len = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, (size_t)len);
		close(fd);
		archive_entry_free(entry);
	}
	archive_write_close(a);
	archive_write_free(a);

	/* create dirs, if necessary */
	if (atom->BUILDID > 0)
		i = snprintf(buf, sizeof(buf), "%s/%s/%s",
					 args->binpkg->path, atom->CATEGORY, atom->PN);
	else
		i = snprintf(buf, sizeof(buf), "%s/%s",
					 args->binpkg->path, atom->CATEGORY);
	mkdir_p(buf, 0755);

	if (atom->BUILDID > 0)
		snprintf(buf + i, sizeof(buf) - i, "/%s-%u.gpkg.tar",
				 atom->PF, atom->BUILDID);
	else
		snprintf(buf + i, sizeof(buf) - i, "/%s.gpkg.tar", atom->PF);

	if (rename(gpkg, buf)) {
		warnp("could not move '%s' to '%s'", gpkg, buf);
		return 1;
	}

	rm_rf(tmpdir);

	if (stat(buf, &st) == -1) {
		warnp("could not stat '%s': %s", buf, strerror(errno));
		return 1;
	}

	printf("%s%s%s KiB\n",
			RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	return 0;
#else
	warnp("gpkg support not compiled in");
	return 1;
#endif
}

static int
qpkg_make(tree_pkg_ctx *pkg, qpkg_cb_args *args)
{
	FILE *out;
	FILE *fp;
	char tmpdir[BUFSIZE];
	char filelist[BUFSIZE + 32];
	char tbz2[BUFSIZE + 32];
	char buf[BUFSIZE * 4];
	size_t xpaksize;
	char *line;
	char *savep;
	int i;
	char *xpak_argv[2];
	struct stat st;
	mode_t mask;
	depend_atom *atom = tree_get_atom(pkg, false);

	if (pretend) {
		printf(" %s-%s %s:\n",
				GREEN, NORM, atom_format("%[CATEGORY]%[PF]%[BUILDID]", atom));
		return 0;
	}

	line = tree_pkg_meta_get(pkg, CONTENTS);
	if (line == NULL)
		return -1;

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", args->binpkg->path);
	mask = umask(0077);
	i = mkstemp(tmpdir);
	umask(mask);
	if (i == -1)
		return -2;
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750))
		return -3;

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL)
		return -4;

	for (; (line = strtok_r(line, "\n", &savep)) != NULL; line = NULL) {
		contents_entry *e;
		e = contents_parse_line(line);
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

	printf(" %s-%s %s: ", GREEN, NORM,
			atom_format("%[CATEGORY]%[PF]%[BUILDID]", atom));
	fflush(stdout);

	snprintf(tbz2, sizeof(tbz2), "%s/bin.tbz2", tmpdir);
	if (snprintf(buf, sizeof(buf), "tar jcf '%s' --files-from='%s' "
			"--no-recursion >/dev/null 2>&1", tbz2, filelist) >
			(int)sizeof(buf) || (fp = popen(buf, "r")) == NULL)
		return 2;
	pclose(fp);

	if ((i = open(tbz2, O_WRONLY)) < 0) {
		warnp("failed to open '%s': %s", tbz2, strerror(errno));
		return 1;
	}

	/* get offset where xpak will start */
	if (fstat(i, &st) == -1) {
		warnp("could not stat '%s': %s", tbz2, strerror(errno));
		close(i);
		return 1;
	}
	xpaksize = st.st_size;

	snprintf(buf, sizeof(buf), "%s/%s/%s",
			args->vdb->path, atom->CATEGORY, atom->PF);
	xpak_argv[0] = buf;
	xpak_argv[1] = NULL;
	xpak_create(AT_FDCWD, tbz2, 1, xpak_argv, 1, verbose);

	/* calculate the number of bytes taken by the xpak archive */
	if (fstat(i, &st) == -1) {
		warnp("could not stat '%s': %s", tbz2, strerror(errno));
		close(i);
		return 1;
	}
	xpaksize = st.st_size - xpaksize;

	/* save tbz2 tail: OOOOSTOP */
	if ((fp = fdopen(i, "a")) == NULL) {
		warnp("could not open '%s': %s", tbz2, strerror(errno));
		close(i);
		return 1;
	}

	WRITE_BE_INT32(buf, xpaksize);
	fwrite(buf, 1, 4, fp);
	fwrite("STOP", 1, 4, fp);
	fclose(fp);

	unlink(filelist);

	/* create dirs, if necessary */
	if (atom->BUILDID > 0)
		i = snprintf(buf, sizeof(buf), "%s/%s/%s",
					 args->binpkg->path, atom->CATEGORY, atom->PN);
	else
		i = snprintf(buf, sizeof(buf), "%s/%s",
					 args->binpkg->path, atom->CATEGORY);
	mkdir_p(buf, 0755);

	if (atom->BUILDID > 0)
		snprintf(buf + i, sizeof(buf) - i, "/%s-%u.xpak",
				 atom->PF, atom->BUILDID);
	else
		snprintf(buf + i, sizeof(buf) - i, "/%s.tbz2", atom->PF);
	if (rename(tbz2, buf)) {
		warnp("could not move '%s' to '%s'", tbz2, buf);
		return 1;
	}

	rmdir(tmpdir);

	if (stat(buf, &st) == -1) {
		warnp("could not stat '%s': %s", buf, strerror(errno));
		return 1;
	}

	printf("%s%s%s KiB\n",
			RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	return 0;
}

static int
qpkg_cb(tree_pkg_ctx *pkg, void *priv)
{
	qpkg_cb_args *args = priv;

	/* check atoms to compute a build-id */
	if (contains_set("binpkg-multi-instance", features)) {
		depend_atom    *atom = tree_get_atom(pkg, false);
		tree_match_ctx *m    = tree_match_atom(args->binpkg, atom,
											   TREE_MATCH_FIRST);
		if (m != NULL) {
			atom->BUILDID = m->atom->BUILDID;
			tree_match_close(m);
		}

		/* take the next, we should always start at 1, so either way
		 * this is fine */
		atom->BUILDID++;
	}

	if (args->build_gpkg) {
		if (qgpkg_make(pkg, args) == 0)
			args->pkgs_made++;
	} else {
		if (qpkg_make(pkg, args) == 0)
			args->pkgs_made++;
	}

	return 0;
}

int qpkg_main(int argc, char **argv)
{
	size_t s;
	int i;
	struct stat st;
	depend_atom *atom;
	int restrict_chmod = 0;
	int qclean = 0;
	int fd;
	char bindir[_Q_PATH_MAX];
	qpkg_cb_args cb_args;

	memset(&cb_args, 0, sizeof(cb_args));

	cb_args.bindir     = pkgdir;
	cb_args.build_gpkg = strcmp(binpkg_format, "gpkg") == 0;

	while ((i = GETOPT_LONG(QPKG, qpkg, "")) != -1) {
		switch (i) {
		case 'E': cb_args.clean_notintree = true;  /* fall through */
		case 'c': qclean = 1;                       break;
		case 'g': cb_args.build_gpkg = true;        break;
		case 'x': cb_args.build_gpkg = false;       break;
		case 'p': pretend = 1;                      break;
		case 'P':
			restrict_chmod = 1;
			cb_args.bindir = optarg;
			if (access(cb_args.bindir, W_OK) != 0)
				errp("%s", cb_args.bindir);
			break;
		COMMON_GETOPTS_CASES(qpkg)
		}
	}

	/* setup temp dirs */
	if (cb_args.bindir[0] != '/')
		err("'%s' is not a valid package destination", cb_args.bindir);
	/* brute force just unlink any file or symlink, if this fails, it's
	 * actually good :) */
	snprintf(bindir, sizeof(bindir), "%s%s", portroot, cb_args.bindir);
	unlink(bindir);
	fd = open(bindir, O_RDONLY);
	if ((fd == -1 && mkdir(bindir, 0750) == -1) ||
			(fd != -1 && (fstat(fd, &st) == -1 || !S_ISDIR(st.st_mode))))
	{
		errp("could not create packages directory '%s'", bindir);
	}
	if (fd >= 0) {
		/* fd is valid, pointing to a directory */
		if (!restrict_chmod)
			if (fchmod(fd, 0750) < 0)
				errp("could not chmod(0750) packages directory '%s'", bindir);
		close(fd);
	}
	cb_args.binpkg = tree_open_binpkg(portroot, cb_args.bindir);
	if (cb_args.binpkg == NULL)
		return EXIT_FAILURE;

	cb_args.vdb = tree_open_vdb(portroot, portvdb);
	if (!cb_args.vdb)
		return EXIT_FAILURE;

	if (qclean) {
		int ret = qpkg_clean(&cb_args);
		tree_close(cb_args.vdb);
		tree_close(cb_args.binpkg);
		return ret;
	}

	if (argc == optind) {
		tree_close(cb_args.vdb);
		tree_close(cb_args.binpkg);
		qpkg_usage(EXIT_FAILURE);
	}

	/* we have to change to the root so that we can feed the full paths
	 * to tar when we create the binary package. */
	xchdir(portroot);

	/* first process any arguments which point to /var/db/pkg, an
	 * undocumented method to allow easily tab-completing into vdb as
	 * arguments, the trailing / needs to be present for this (as tab
	 * completion would do) */
	s = strlen(portvdb);
	for (i = optind; i < argc; i++) {
		size_t asize = strlen(argv[i]);
		if (asize == 0) {
			argv[i] = NULL;
			continue;
		}
		if (asize > s && argv[i][0] == '/' && argv[i][asize - 1] == '/') {
			char *path = argv[i];

			/* chop off trailing / */
			argv[i][asize - 1] = '\0';

			/* eliminate duplicate leading /, we know it starts with / */
			while (path[1] == '/')
				path++;

			if (strncmp(portvdb, path, s) == 0) {
				path += s + 1 /* also eat / after portvdb */;
				argv[i] = path;
			} else {
				argv[i][asize - 1] = '/';  /* restore, it may be a cat match */
			}
		}
	}

	/* now try to run through vdb and locate matches for user inputs */
	for (i = optind; i < argc; i++) {
		if (argv[i] == NULL)
			continue;
		if (strcmp(argv[i], "world") == 0) {
			/* this is a crude hack, we include all packages for this,
			 * which isn't exactly @world, but all its deps too */
			tree_foreach_pkg_fast(cb_args.vdb, qpkg_cb, &cb_args, NULL);
			break;  /* no point in continuing since we did everything */
		}
		atom = atom_explode(argv[i]);
		if (atom == NULL)
			continue;

		s = cb_args.pkgs_made;
		tree_foreach_pkg_fast(cb_args.vdb, qpkg_cb, &cb_args, atom);
		if (s == cb_args.pkgs_made)
			warn("no match for '%s'", argv[i]);
		atom_implode(atom);
	}
	tree_close(cb_args.vdb);
	tree_close(cb_args.binpkg);

	if (cb_args.pkgs_made > 0)
		qprintf(" %s*%s Packages can be found in %s\n",
				GREEN, NORM, cb_args.bindir);

	return (cb_args.pkgs_made > 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
