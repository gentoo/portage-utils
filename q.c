/*
 * Copyright 2005-2026 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd        - <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 * Copyright 2017-     Fabian Groffen  - <grobian@gentoo.org>
 */

#include "main.h"
#include "applets.h"

#include <unistd.h>
#include <strings.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <xalloc.h>

#if defined(__MACH__) && defined(__APPLE__)
#include <libproc.h>
#endif

#if defined(ENABLE_GPKG) || defined(ENABLE_GTREE)
# include <archive.h>
# include <archive_entry.h>
#endif

#include "atom.h"
#include "basename.h"
#include "eat_file.h"
#include "human_readable.h"
#include "rmspace.h"
#include "scandirat.h"
#include "tree.h"
#include "xmkdir.h"

#define Q_FLAGS "cij:oem" COMMON_FLAGS
static struct option const q_long_opts[] = {
	{"build-cache",   no_argument, NULL, 'c'},
	{"install",       no_argument, NULL, 'i'},
	{"jobserver",      a_argument, NULL, 'j'},
	{"overlays",      no_argument, NULL, 'o'},
	{"envvar",        no_argument, NULL, 'e'},
	{"masks",         no_argument, NULL, 'm'},
	COMMON_LONG_OPTS
};
static const char * const q_opts_help[] = {
	"(Re)Build ebuild/metadata cache for all available overlays",
	"Install symlinks for applets",
	"Run minimalistic jobserver for given jobs",
	"Print available overlays (read from repos.conf)",
	"Print used variables and their found values",
	"Print (package.)masks for the current profile",
	COMMON_OPTS_HELP
};
#define q_usage(ret) usage(ret, Q_FLAGS, q_long_opts, q_opts_help, NULL, lookup_applet_idx("q"))

APPLET lookup_applet(const char *applet)
{
	unsigned int i;

	if (strlen(applet) < 1)
		return NULL;

	if (applet[0] == 'q')
		applet++;

	/* simple case, e.g. something like "qlop" or "q lop" both being "lop"  */
	for (i = 0; applets[i].desc != NULL; i++) {
		if (strcmp(applets[i].name + 1, applet) == 0) {
			argv0 = applets[i].name;
			return applets[i].func;
		}
	}

	/* this is possibly an alias like "belongs"
	 * NOTE: we continue where the previous loop left, e.g. on the first
	 * alias (desc == NULL) */
	for (/*i*/; applets[i].name != NULL; i++) {
		if (strcmp(applets[i].name, applet) == 0) {
			unsigned int j;

			/* lookup the real name for this alias */
			for (j = 1; applets[j].desc != NULL; j++) {
				if (applets[j].func == applets[i].func) {
					argv0 = applets[j].name;
					return applets[j].func;
				}
			}

			/* this shouldn't happen and means our array from applets.h
			 * is inconsistent */
			warn("invalid applet alias '%s': target applet unknown", applet);
			return NULL;
		}
	}

	/* still nothing ?  those bastards ... */
	warn("unknown applet: %s", applet);
	return NULL;
}

int lookup_applet_idx(const char *applet)
{
	unsigned int i;
	for (i = 0; applets[i].name; i++)
		if (strcmp(applets[i].name, applet) == 0)
			return i;
	return 0;
}

#ifdef ENABLE_GTREE
struct q_cache_ctx {
	struct archive *archive;
	time_t          buildtime;
	char           *cbuf;
	size_t          cbufsiz;
	size_t          cbuflen;
	char            last_pkg[_Q_PATH_MAX];
};
static int q_build_gtree_pkg_process_dir(struct q_cache_ctx *ctx,
										 char               *path,
										 char               *pbuf,
										 size_t              pbufsiz,
										 int                 dfd)
{
	struct archive       *a     = ctx->archive;
	struct archive_entry *entry = NULL;
	struct dirent       **flist = NULL;
	struct stat           st;
	int                   fcnt;
	int                   i;
	int                   fd = -1;
	size_t                len;
	ssize_t               rlen;
	char                  buf[BUFSIZ];

	fcnt = scandirat(dfd, ".", &flist, filter_self_parent, alphasort);

	for (i = 0; i < fcnt; i++) {
		/* skip Manifests here, the whole bundle should be consistent */
		if (strcmp(flist[i]->d_name, "Manifest.gz") == 0)
			continue;
		if (fd >= 0)
			close(fd);
		fd = openat(dfd, flist[i]->d_name, O_RDONLY);
		if (fd < 0 || fstat(fd, &st) < 0)
			continue;
		len = snprintf(pbuf, pbufsiz, "/%s", flist[i]->d_name);
		if (len >= pbufsiz) {
			/* oops, doesn't fit, don't crash, just skip */
			warn("file %s too long for path %s", flist[i]->d_name, path);
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			q_build_gtree_pkg_process_dir(ctx, path,
										  pbuf + len, pbufsiz - len, fd);
			continue;
		}
		/* the rest, record an entry */
		entry = archive_entry_new();
		archive_entry_set_pathname(entry, path);
		archive_entry_set_size(entry, st.st_size);
		archive_entry_set_mtime(entry, ctx->buildtime, 0);
		archive_entry_set_filetype(entry, st.st_mode);
		archive_entry_set_perm(entry, 0644);
		archive_write_header(a, entry);
		while ((rlen = read(fd, buf, sizeof(buf))) > 0)
			archive_write_data(a, buf, rlen);
		archive_entry_free(entry);
	}
	if (fd >= 0)
		close(fd);

	scandir_free(flist, fcnt);

	return 0;
}
static int q_build_gtree_cache_pkg(tree_pkg_ctx *pkg, void *priv)
{
	struct q_cache_ctx   *ctx   = priv;
	struct archive       *a     = ctx->archive;
	struct archive_entry *entry;
	depend_atom          *atom  = tree_get_atom(pkg, false);
	char                  buf[_Q_PATH_MAX];
	char                 *qc;
	size_t                qclen;

	/* construct the common prefix */
	snprintf(buf, sizeof(buf), "caches/%s/%s", atom->CATEGORY, atom->PF);

	/* keys from md5-cache except _md5_ and repository (the latter is
	 * stored at the top level)
	 * all of this is stored as key-value file, because storing it as
	 * individual keys takes much more storage for no particular benefit */

	/* start over, reusing previous buf allocation */
	ctx->cbuflen = 0;

#define q_cache_add_cache_entry_val(K,V) \
	do { \
		qc = V; \
		if (qc != NULL) \
			qclen = strlen(qc); \
		else \
			qclen = 0; \
		if (qclen > 0) { \
			qclen += sizeof(#K) - 1 + 1 /*=*/; \
			if (ctx->cbuflen + qclen + 1 > ctx->cbufsiz) { \
				ctx->cbufsiz = ctx->cbuflen + qclen + 1; \
				ctx->cbufsiz = ((ctx->cbufsiz + (1024 - 1)) / 1024) * 1024; \
				ctx->cbuf    = xrealloc(ctx->cbuf, ctx->cbufsiz); \
			} \
			ctx->cbuflen += snprintf(ctx->cbuf + ctx->cbuflen, \
									 ctx->cbufsiz - ctx->cbuflen, \
									 #K "=%s\n", qc); \
		} \
	} while (false)
#define q_cache_add_cache_entry(K) \
	if (Q_##K != Q__md5_ && \
		Q_##K != Q_repository) \
		q_cache_add_cache_entry_val(K, tree_pkg_meta(pkg, Q_##K));

	/* try all known keys */
	TREE_META_KEYS(q_cache_add_cache_entry);

	/* note: _eclasses_ includes md5 hashes, which are redundant/not
	 * necessary, we could remove it, but then we'd need alternative
	 * code, so let's keep everything the same */
#undef q_cache_add_cache_entry
#undef q_cache_add_cache_entry_val

	entry = archive_entry_new();
	archive_entry_set_pathname(entry, buf);
	archive_entry_set_size(entry, ctx->cbuflen);
	archive_entry_set_mtime(entry, ctx->buildtime, 0);
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_entry_set_perm(entry, 0644);
	archive_write_header(a, entry);
	archive_write_data(a, ctx->cbuf, ctx->cbuflen);
	archive_entry_free(entry);

	return 0;
}
static int q_build_gtree_ebuilds_pkg(tree_pkg_ctx *pkg, void *priv)
{
	struct q_cache_ctx   *ctx   = priv;
	struct archive       *a     = ctx->archive;
	struct archive_entry *entry;
	struct stat           st;
	depend_atom          *atom  = tree_get_atom(pkg, false);
	char                  buf[_Q_PATH_MAX];
	char                 *p;
	size_t                siz;
	size_t                len;
	char                 *qc;
	char                  pth[_Q_PATH_MAX];
	size_t                flen;
	int                   ffd;
	bool                  newpkg = true;

	/* nothing to do if not an ebuild tree
	 * we could technically pull the ebuild from the VDB, or maybe
	 * from the binpkg, but for what use? only an ebuild tree is
	 * meant to be built from, others only use metadata */
	if (tree_pkg_get_treetype(pkg) != TREETYPE_EBUILD)
		return 0;

	/* construct the common prefix */
	len = snprintf(buf, sizeof(buf), "ebuilds/%s/%s/",
				   atom->CATEGORY, atom->PN);
	p   = buf + len;
	siz = sizeof(buf) - len;

	if (strcmp(ctx->last_pkg, buf + (sizeof("ebuilds/") - 1)) != 0)
		snprintf(ctx->last_pkg, sizeof(ctx->last_pkg),
				 "%s", buf + (sizeof("ebuilds/") - 1));
	else
		newpkg = false;

	if (newpkg) {
		int  dfd;

		snprintf(pth, sizeof(pth), "%s", tree_pkg_get_path(pkg));
		qc = strrchr(pth, '/');
		if (qc == NULL)
			return 1;  /* on an ebuild tree this certainly is invalid */
		*qc = '\0';
		dfd = openat(tree_pkg_get_portroot_fd(pkg), pth, O_RDONLY);
		if (dfd < 0)
			return 1;  /* how? */

		ffd = openat(dfd, "metadata.xml", O_RDONLY);
		if (ffd >= 0) {
			if (fstat(ffd, &st) == 0) {
				entry = archive_entry_new();
				snprintf(p, siz, "metadata.xml");
				archive_entry_set_pathname(entry, buf);
				archive_entry_set_size(entry, st.st_size);
				archive_entry_set_mtime(entry, ctx->buildtime, 0);
				archive_entry_set_filetype(entry, AE_IFREG);
				archive_entry_set_perm(entry, 0644);
				archive_write_header(a, entry);
				while ((flen = read(ffd, pth, sizeof(pth))) > 0)
					archive_write_data(a, pth, flen);
				archive_entry_free(entry);
			}
			close(ffd);
		}
		/* for Manifest file we perform a "grep" here on the only
		 * relevant entries: DIST, this reduces the overall size
		 * of the tree considerably */
		if (eat_file_at(dfd, "Manifest", &ctx->cbuf, &ctx->cbufsiz)) {
			bool  start = true;
			bool  write = false;
			char *wp;
			for (qc = ctx->cbuf, wp = ctx->cbuf; *qc != '\0'; qc++) {
				if (start && strncmp(qc, "DIST ", 5) == 0)
					write = true;
				start = false;
				if (write)
					*wp++ = *qc;
				if (*qc == '\r' || *qc == '\n') {
					start = true;
					write = false;
				}
			}
			ctx->cbuflen = wp - ctx->cbuf;

			if (ctx->cbuflen > 0) {
				entry = archive_entry_new();
				snprintf(p, siz, "Manifest");
				archive_entry_set_pathname(entry, buf);
				archive_entry_set_size(entry, ctx->cbuflen);
				archive_entry_set_mtime(entry, ctx->buildtime, 0);
				archive_entry_set_filetype(entry, AE_IFREG);
				archive_entry_set_perm(entry, 0644);
				archive_write_header(a, entry);
				archive_write_data(a, ctx->cbuf, ctx->cbuflen);
				archive_entry_free(entry);
			}
		}
		/* process files, unfortunately this can be any number of
		 * directories deep (remember eblitz?) so we'll have to recurse
		 * for this one */
		flen = snprintf(p, siz, "files");
		ffd  = openat(dfd, "files", O_RDONLY);
		if (ffd >= 0) {
			q_build_gtree_pkg_process_dir(ctx,
										  buf, p + flen, siz - flen,
										  ffd);
			close(ffd);
		}

		close(dfd);
	}

	snprintf(pth, sizeof(pth), "%s.ebuild", atom->PF);
	ffd = openat(tree_pkg_get_portroot_fd(pkg),
				 tree_pkg_get_path(pkg), O_RDONLY);
	if (ffd >= 0) {
		if (fstat(ffd, &st) == 0) {
			entry = archive_entry_new();
			snprintf(p, siz, "%s.ebuild", atom->PF);
			archive_entry_set_pathname(entry, buf);
			archive_entry_set_size(entry, st.st_size);
			archive_entry_set_mtime(entry, ctx->buildtime, 0);
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);
			archive_write_header(a, entry);
			while ((flen = read(ffd, pth, sizeof(pth))) > 0)
				archive_write_data(a, pth, flen);
			archive_entry_free(entry);
		}
		close(ffd);
	}

	return 0;
}
#endif

static bool q_js_shutdown = false;
static void q_js_sighandler(int sig)
{
	switch (sig) {
		case SIGINT:
		case SIGTERM:
		case SIGSEGV:
		case SIGBUS:
		case SIGABRT:
		case SIGFPE:
		case SIGILL:
			q_js_shutdown = true;
			break;
	}
}

static int q_jobserver(char *path, int njobs)
{
	int       pipefds[2];
	int       i;

	/* install signal handlers so we can cleanup on exit */
	signal(SIGINT,  q_js_sighandler);
	signal(SIGTERM, q_js_sighandler);
	signal(SIGSEGV, q_js_sighandler);
	signal(SIGBUS,  q_js_sighandler);
	signal(SIGABRT, q_js_sighandler);
	signal(SIGFPE,  q_js_sighandler);
	signal(SIGILL,  q_js_sighandler);

	pipefds[0] = open(path, O_RDONLY | O_NONBLOCK);
	if (pipefds[0] < 0)
		return 1;
	pipefds[1] = open(path, O_WRONLY);
	if (pipefds[1] < 0) {
		close(pipefds[0]);
		return 1;
	}

	for (i = 0; i < njobs; i++)
		if (write(pipefds[1], "q", 1) != 1)
			i--;  /* this is close to impossible though */

	while (!q_js_shutdown)
		sleep(1);

	close(pipefds[1]);
	close(pipefds[0]);

	return 0;
}

int q_main(int argc, char **argv)
{
	int i;
	bool build_cache;
	bool install;
	bool run_jobserver;
	bool print_overlays;
	bool print_vars;
	bool print_masks;
	const char *p;
	const char *jobs;
	APPLET func;

	if (argc == 0)
		return 1;

	argv0 = p = basename(argv[0]);

	if ((func = lookup_applet(p)) == NULL)
		return 1;
	if (strcmp("q", p) != 0)
		return (func)(argc, argv);

	if (argc == 1)
		q_usage(EXIT_FAILURE);

	build_cache    = false;
	install        = false;
	run_jobserver  = false;
	print_overlays = false;
	print_vars     = false;
	print_masks    = false;
	while ((i = GETOPT_LONG(Q, q, "+")) != -1) {
		switch (i) {
		COMMON_GETOPTS_CASES(q)
		case 'c': build_cache    = true;   break;
		case 'i': install        = true;   break;
		case 'j': run_jobserver  = true;
				  jobs           = optarg; break;
		case 'o': print_overlays = true;   break;
		case 'e': print_vars     = true;   break;
		case 'm': print_masks    = true;   break;
		}
	}

	if (install) {
		char buf[_Q_PATH_MAX];
		const char *prog, *dir;
		ssize_t rret;
		int fd, ret;

		if (!quiet)
			printf("Installing symlinks:\n");

#if defined(__MACH__) && defined(__APPLE__)
		rret = proc_pidpath(getpid(), buf, sizeof(buf));
		if (rret != -1)
			rret = strlen(buf);
#elif defined(__sun) && defined(__SVR4)
		prog = getexecname();
		rret = strlen(prog);
		if ((size_t)rret > sizeof(buf) - 1) {
			rret = -1;
		} else {
			snprintf(buf, sizeof(buf), "%s", prog);
		}
#else
		rret = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
#endif
		if (rret == -1) {
			warnfp("haha no symlink love for you ... :(");
			return 1;
		}
		buf[rret] = '\0';

		prog = basename(buf);
		dir = dirname(buf);
		fd = open(dir, O_RDONLY|O_CLOEXEC|O_PATH);
		if (fd < 0) {
			warnfp("open(%s) failed", dir);
			return 1;
		}

		ret = 0;
		for (i = 1; applets[i].desc; ++i) {
			int r = symlinkat(prog, fd, applets[i].name);
			if (!quiet)
				printf(" %s ...\t[%s]\n",
						applets[i].name, r ? strerror(errno) : "OK");
			if (r && errno != EEXIST)
				ret = 1;
		}

		close(fd);

		return ret;
	}

	if (print_overlays) {
		char *overlay;
		char *repo_name = NULL;
		size_t repo_name_len = 0;
		char buf[_Q_PATH_MAX];
		size_t n;

		array_for_each(overlays, n, overlay) {
			repo_name = array_get(overlay_names, n);
			if (strcmp(repo_name, "<PORTDIR>") == 0) {
				repo_name = NULL;
				snprintf(buf, sizeof(buf), "%s/profiles/repo_name", overlay);
				if (!eat_file(buf, &repo_name, &repo_name_len)) {
					free(repo_name);
					repo_name = NULL;
				}
				if (repo_name != NULL)
					rmspace(repo_name);
			}
			printf("%s%s%s: %s%s%s%s",
					GREEN, repo_name == NULL ? "?unknown?" : repo_name,
					NORM, overlay,
					YELLOW, main_overlay == overlay ? " (main)" : "", NORM);
			if (verbose)
				printf(" [%s]\n", (char *)array_get(overlay_src, n));
			else
				printf("\n");
			if (repo_name_len != 0) {
				free(repo_name);
				repo_name_len = 0;
			}
		}

		return 0;
	}

	if (print_vars) {
		env_vars *var;
		int j;

		if (argc == optind || argc - optind > 1) {
			for (i = 0; vars_to_read[i].name; i++) {
				var = &vars_to_read[i];

				/* check if we want this variable */
				for (j = optind; j < argc; j++)
					if (strcmp(var->name, argv[j]) == 0)
						break;
				if (j == argc && optind != argc)
					continue;

				printf("%s%s%s=", BLUE, var->name, NORM);
				switch (var->type) {
					case _Q_BOOL:
						printf("%s%s%s",
								YELLOW, *var->value.b ? "1" : "0", NORM);
						break;
					case _Q_STR:
					case _Q_ISTR:
						printf("%s\"%s\"%s", RED, *var->value.s, NORM);
						break;
					case _Q_ISET: {
						array *vals = array_new();
						size_t n;
						char  *val;

						printf("%s\"", RED);
						array_set(*var->value.t, vals);
						array_for_each(vals, n, val) {
							printf("%s%s", n == 0 ? "" : " ", val);
						}
						array_free(vals);
						printf("\"%s", NORM);
					}	break;
				}
				if (verbose)
					printf(" [%s]\n", var->src);
				else
					printf("\n");
			}
		} else {
			/* single envvar printing, just output the value, like
			 * portageq envvar does */
			for (i = 0; vars_to_read[i].name; i++) {
				var = &vars_to_read[i];

				if (strcmp(var->name, argv[optind]) != 0)
					continue;

				switch (var->type) {
					case _Q_BOOL:
						printf("%s%s%s",
							   YELLOW, *var->value.b ? "1" : "0", NORM);
						break;
					case _Q_STR:
					case _Q_ISTR:
						printf("%s%s%s", RED, *var->value.s, NORM);
						break;
					case _Q_ISET: {
						array *vals = array_new();
						size_t n;
						char  *val;

						array_set(*var->value.t, vals);
						array_for_each(vals, n, val) {
							printf("%s%s", n == 0 ? RED : " ", val);
						}
						array_free(vals);
						printf("%s", NORM);
					}	break;
				}
				if (verbose)
					printf(" [%s]\n", var->src);
				else
					printf("\n");
			}
		}

		return 0;
	}

	if (print_masks) {
		array *masks = array_new();
		array *files = array_new();
		char *mask;
		size_t n;
		int j;
		bool match;
		char *lastmfile = NULL;
		long lastcbeg = 0;
		long lastcend = 0;
		char *buf = NULL;
		size_t buflen = 0;
		depend_atom *atom;
		depend_atom *qatom;

		array_set(package_masks, masks);
		values_set(package_masks, files);

		array_for_each(masks, n, mask) {
			if ((atom = atom_explode(mask)) == NULL)
				continue;

			match = true;
			if (argc > optind) {
				match = false;
				for (j = optind; j < argc; j++) {
					qatom = atom_explode(argv[j]);
					if (qatom != NULL && atom_compare(atom, qatom) == EQUAL)
						match = true;
					atom_implode(qatom);
					if (match)
						break;
				}
			}
			if (!match)
				continue;

			if (verbose > 1) {
				char *mfile = (char *)array_get(files, n);
				char *l;
				char *s = NULL;
				long line = 0;
				long cbeg = 0;
				long cend = 0;

				s = l = strchr(mfile, ':');
				/* p cannot be NULL, just crash if something's wrong */
				(void)strtol(l + 1, &l, 10);
				if (*l == ':')
					cbeg = strtol(l + 1, &l, 10);
				if (*l == '-')
					cend = strtol(l + 1, &l, 10);
				if (cend < cbeg)
					cend = cbeg = 0;

				if (lastmfile == NULL ||
						strncmp(lastmfile, mfile, s - mfile + 1) != 0 ||
						lastcbeg != cbeg || lastcend != cend)
				{
					char mfileloc[_Q_PATH_MAX];

					snprintf(mfileloc, sizeof(mfileloc), "%s%.*s",
							 portroot, (int)(s - mfile), mfile);

					if (buf != NULL)
						*buf = '\0';
					eat_file(mfileloc, &buf, &buflen);

					line = 0;
					for (l = buf; (s = strchr(l, '\n')) != NULL; l = s + 1)
					{
						line++;
						if (line >= cbeg && line <= cend)
							printf("%.*s\n", (int)(s - l), l);
						if (line > cend)
							break;
					}
				}
				lastmfile = mfile;
				lastcbeg = cbeg;
				lastcend = cend;
			}
			printf("%s", atom_format(
						"%[pfx]%[CAT]%[PF]%[SLOT]%[SUBSLOT]%[sfx]%[USE]%[REPO]",
						atom));

			if (verbose == 1) {
				printf(" [%s]\n", (char *)array_get(files, n));
			} else {
				printf("\n");
			}
			atom_implode(atom);
		}

		if (buf != NULL)
			free(buf);

		array_free(masks);
		array_free(files);

		return 0;
	}

	if (build_cache) {
#ifdef ENABLE_GTREE
		/* traverse all overlays, create a cache for each
		 * the cache basically is one giant tar with:
		 * - gtree-1  (mandatory, first file ident)
		 * - repo.tar{compr}
		 *   - repository
		 *   - cache/CAT/PF  (extracted info from the ebuild)
		 *   - ebuilds/CAT/PN
		 *     + PF.ebuild (the file from the tree) (repeated for each PF)
		 *     + metadata.xml (the file from the tree)
		 *     + Manifest (the file from the tree, to verify distfiles)
		 *     + files/ (the directory from the tree)
		 *   - eclasses/ (the directory from the tree)
		 * - repo.tar{compr}.sig
		 * but all of them within are guaranteed to be consistent with
		 * each other (it is one snapshot)
		 * the cache is suitable for distribution
		 *
		 * using the cache to install ebuilds, requires to extract the
		 * ebuild (skipping the cache directory) and the eclasses
		 * (easily found through the _eclasses_ cache key)
		 *
		 * For a Portage or PMS-compatible env this probably means
		 * constructing a tree out of the tar, but this should be a
		 * small price to pay once the whole of the dep-resolving can be
		 * done without questions of validity. */
		char                 *overlay;
		size_t                n;
		tree_ctx             *t;
		struct archive       *a;
		struct archive_entry *entry;
		struct q_cache_ctx    qcctx;
		struct stat           st;
		char                  buf[BUFSIZ];
		size_t                len;
		ssize_t               rlen;
		int                   dfd;
		int                   tfd;
		int                   fd;

		memset(&qcctx, 0, sizeof(qcctx));

		array_for_each(overlays, n, overlay) {
			if (verbose)
				printf("building cache for %s%s%s/metadata/repo.gtree.tar\n",
					   BLUE, overlay, NORM);

			/* we store the cache inside the metadata dir, which means
			 * it gets wiped on portage sync (good because that would
			 * invalidate it) and tree_open can transparently locate and
			 * use it */

			/* ensure we can actually write the new cache */
			len = snprintf(buf, sizeof(buf),
					 	   "%s/%s/metadata", portroot, overlay);
			mkdir_p(buf, 0755);

			snprintf(buf + len, sizeof(buf) - len, "/repo.gtree.tar");
			/* because we're building a new one here, make sure
			 * tree_open doesn't pick it up */
			unlink(buf);

			t = tree_open(portroot, overlay);
			if (t == NULL) {
				warn("could not open overlay at %s", overlay);
				continue;
			}

			/* now open it */
			fd = open(buf, O_WRONLY | O_CREAT | O_TRUNC,
					  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH /* 0644 */);
			if (fd < 0)
			{
				warnp("failed to open gtree for writing at %s", buf);
				tree_close(t);
				continue;
			}

			a = archive_write_new();
			archive_write_set_format_ustar(a);  /* GLEP-78, just to be safe */
			archive_write_open_fd(a, fd);

			qcctx.buildtime = time(NULL);

			/* add marker file, we populate with our version, although
			 * nothing should rely on that */
			len = snprintf(buf, sizeof(buf), "portage-utils-" VERSION);
			entry = archive_entry_new();
			archive_entry_set_pathname(entry, "gtree-1");
			archive_entry_set_size(entry, len);
			archive_entry_set_mtime(entry, qcctx.buildtime, 0);
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);
			archive_write_header(a, entry);
			archive_write_data(a, buf, len);
			archive_entry_free(entry);

			/* repo.tar.zst
			 * the nested archive unfortunately cannot be written
			 * straight to the archive stream above: its size needs to
			 * be known before data can be written, hence we'll have to
			 * produce the archive separately first, which sulks, but ok
			 * in order to kind of protect it from being modified, we
			 * make the file invisible */
			snprintf(buf, sizeof(buf),
					 "%s/%s/metadata/gtree.XXXXXX", portroot, overlay);
			tfd = mkstemp(buf);
			if (tfd < 0) {
				warnp("failed to open temp file");
				tree_close(t);
				archive_write_close(a);
				archive_write_free(a);
				continue;
			}
			unlink(buf);  /* make invisible, drop on close */

			qcctx.archive = archive_write_new();
			archive_write_set_format_ustar(qcctx.archive);
			/* would love to use this:
			 * archive_write_add_filter_zstd(qcctx.archive);
			 * but https://github.com/libarchive/libarchive/issues/957
			 * suggests there's never going to be an interface
			 * for this, which is a real shame */
			archive_write_add_filter_program(qcctx.archive, "zstd -19");
			archive_write_open_fd(qcctx.archive, tfd);

			/* write repo name, if any */
			if (tree_get_repo_name(t) != NULL) {
				char *r = tree_get_repo_name(t);
				len = strlen(r);
				entry = archive_entry_new();
				archive_entry_set_pathname(entry, "repository");
				archive_entry_set_size(entry, len);
				archive_entry_set_mtime(entry, qcctx.buildtime, 0);
				archive_entry_set_filetype(entry, AE_IFREG);
				archive_entry_set_perm(entry, 0644);
				archive_write_header(qcctx.archive, entry);
				archive_write_data(qcctx.archive, r, len);
				archive_entry_free(entry);
			}

			/* add cache and ebuilds */
			tree_foreach_pkg(t, q_build_gtree_cache_pkg, &qcctx, true, NULL);
			qcctx.last_pkg[0] = '\0';
			tree_foreach_pkg(t, q_build_gtree_ebuilds_pkg, &qcctx, true, NULL);

			/* add eclasses */
			snprintf(buf, sizeof(buf), "%s/%s/eclass", portroot, overlay);
			dfd = open(buf, O_RDONLY);
			len = snprintf(buf, sizeof(buf), "eclasses");
			if (dfd >= 0) {
				q_build_gtree_pkg_process_dir(&qcctx, buf,
											  buf + len,
											  sizeof(buf) - len,
											  dfd);
				close(dfd);
			}

			archive_write_close(qcctx.archive);
			archive_write_free(qcctx.archive);

			/* now we got the size, put it in the main archive */
			fstat(tfd, &st);
			entry = archive_entry_new();
			archive_entry_set_pathname(entry, "repo.tar.zst");
			archive_entry_set_size(entry, st.st_size);
			archive_entry_set_mtime(entry, qcctx.buildtime, 0);
			archive_entry_set_filetype(entry, AE_IFREG);
			archive_entry_set_perm(entry, 0644);
			archive_write_header(a, entry);
			lseek(tfd, 0, SEEK_SET);  /* reposition at the start of file */
			while ((rlen = read(tfd, buf, sizeof(buf))) > 0)
				archive_write_data(a, buf, rlen);
			archive_entry_free(entry);

			/* TODO: compute and put .sig in here */

			/* cleanup repo archive */
			close(tfd);

			archive_write_close(a);
			archive_write_free(a);

			if (verbose) {
				char *r = tree_get_repo_name(t);
				if (fstat(fd, &st) < 0)
					warnp("could not stat produced archive");
				else
					printf("%s%s%s: %s%siB%s\n",
						   GREEN, r == NULL ? "???" : r, NORM,
						   RED, make_human_readable_str(st.st_size,
														1, 0), NORM);
			}

			fchmod(fd, 0644);
			close(fd);
			tree_close(t);
		}

		free(qcctx.cbuf);

		return 0;
#else
		err("gtree support not compiled in");
#endif
	}

	if (run_jobserver) {
		long    njobs = -1;
		char   *lastp = NULL;
		char    jspath[_Q_PATH_MAX];
		char    jslink[_Q_PATH_MAX];
		ssize_t len;
		bool    start_server = false;

		if (jobs == NULL ||
			jobs[0] == '\0' ||
			((njobs = strtol(jobs, &lastp, 10)) == 0 &&
			 errno == EINVAL) ||
			*lastp != '\0' ||
			njobs < 0)
		{
			warn("invalid argument to --jobserver: '%s'", jobs);
			return 1;
		}
		if (njobs == 1)
		{
			warn("number of jobs with --jobserver must be >1");
			return 1;
		}
		if (njobs > 0)
			njobs--;  /* correct for the assumed token Make takes */

#define Q_JOBS_SOCK ".q-jobserver.sock"
		/* placing this in /run is pointless, we are an aid for when
		 * things like steve aren't possible, so likely we run
		 * unprivileged */
		snprintf(jspath, sizeof(jspath),
				 CONFIG_EPREFIX "tmp/" Q_JOBS_SOCK);
		if ((len = readlink(jspath, jslink, sizeof(jslink))) < 0 ||
			len == sizeof(jslink))
		{
			/* no such file, or invalid/unreadable garbage */
			unlink(jspath);
			start_server = true;
		} else {
			jslink[len] = '\0';
			/* see if the target is still alive */
			if (len > sizeof(Q_JOBS_SOCK) - 1 &&
				strncmp(jslink, Q_JOBS_SOCK, sizeof(Q_JOBS_SOCK) - 1) == 0 &&
				jslink[sizeof(Q_JOBS_SOCK) - 1] == '.')
			{
				char *endp;
				long  pid;

				pid = strtol(&jslink[sizeof(Q_JOBS_SOCK)], &endp, 10);
				if (*endp == '\0' &&
					pid != 0)
				{
					/* valid link, expand it */
					snprintf(jslink, sizeof(jslink) - 1,
							 CONFIG_EPREFIX "tmp/" Q_JOBS_SOCK ".%ld",
							 pid);

					/* let's check if the pid is alive */
					if (kill((pid_t)pid, 0) == 0) {
						if (njobs > 0) {
							warn("jobserver process %ld is already running",
								 pid);
						} else { /* must be 0 */
							/* shutdown existing */
							return kill((pid_t)pid, SIGTERM);
						}
					} else {
						/* not there, pronounce dead */
						unlink(jslink);
						unlink(jspath);
						start_server = true;
					}
				} else {
					/* invalid, let's assume the link is supposed to be
					 * ours, so if someone is tinkering with it, YOLO */
					unlink(jspath);
					start_server = true;
				}
			} else {
				/* like above, the link is garbage, but we own it */
				unlink(jspath);
				start_server = true;
			}
		}

		if (start_server) {
			pid_t child;
			int   fds[2];

			if (pipe(fds) != 0) {
				warnp("internal failure trying to start jobserver");
				return 1;
			}

			child = fork();
			if (child < 0) {
				warnp("failed to create jobserver");
				return 1;
			}
			if (child > 0) {
				/* parent, read from pipe so we exit when the child
				 * really started up */
				close(fds[1]);
				if ((len = read(fds[0], jslink, sizeof(jslink))) < 0)
				{
					warnp("failed to start jobserver");
					return 1;
				}
				if (len <= 3 ||
					jslink[0] != 'O' ||
					jslink[1] != 'K' ||
					jslink[2] != ':')
				{
					kill(child, SIGKILL);
					warnp("internal failure while starting jobserver");
					return 1;
				}

				memmove(&jslink[0], &jslink[3], len - 3);
				jslink[len - 3] = '\0';
			} else {
				int ret;

				/* child */
				close(fds[0]);
				/* start a new session, so we can properly detach from
				 * our parent */
				if (setsid() < 0)
					errp("failed to create session");
				child = fork();
				if (child != 0)
					close(fds[1]);
				if (child < 0)
					errp("failed to fork daemon process");
				if (child > 0)
					return 0;

				snprintf(jslink, sizeof(jslink),
						 CONFIG_EPREFIX "tmp/" Q_JOBS_SOCK ".%u", getpid());
				if (mkfifo(jslink, 0666) != 0)
					errp("failed to create jobserver fifo '%s'", jslink);
				if (symlink(&jslink[sizeof(CONFIG_EPREFIX "tmp/") - 1],
							jspath) != 0)
				{
					warnp("failed to create jobserver symlink '%s'", jspath);
					unlink(jslink);
					return 1;
				}

				/* tell grandparent we've made it */
				if (write(fds[1], "OK:", 3) != 3 ||
					write(fds[1], jslink, strlen(jslink)) != strlen(jslink))
					warnp("could not report success");
				close(fds[1]);
				/* close stdio streams */
				close(0);
				close(1);
				close(2);

				/* finally run the jobserver */
				ret = q_jobserver(jslink, (int)njobs);

				unlink(jslink);
				unlink(jspath);
				return ret;
			}
		}

		/* success (existing or just started server), return path */
		printf("fifo:%s\n", jslink);

		return 0;
	}

	if (argc == optind)
		q_usage(EXIT_FAILURE);
	if ((func = lookup_applet(argv[optind])) == NULL)
		return 1;

	/* In case of "q --option ... appletname ...", remove appletname from the
	 * applet's args. */
	if (optind > 1) {
		argv[0] = argv[optind];
		for (i = optind; i < argc; ++i)
			argv[i] = argv[i + 1];
	} else
		++argv;

	optind = 0; /* reset so the applets can call getopt */

	return (func)(argc - 1, argv);
}
